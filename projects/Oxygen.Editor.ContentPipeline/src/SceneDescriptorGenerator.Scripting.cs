// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Projects the scene's script slots into the bindings of its script sidecar job.
/// </summary>
public sealed partial class SceneDescriptorGenerator
{
    /// <summary>Builds the script bindings of a scene from its nodes' slots.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="scope">The cook scope.</param>
    /// <param name="sceneInput">The scene being cooked.</param>
    /// <param name="nodeIndices">Each node's index in the cooked scene.</param>
    /// <param name="operationId">The cook operation.</param>
    /// <param name="diagnostics">Receives slot problems.</param>
    /// <returns>The bindings, their sites and the scripts and assets they use.</returns>
    private static (List<ScriptSidecarBinding> Bindings, List<ScriptBindingSite> Sites, List<Uri> References) CreateScriptBindings(
        Scene scene,
        ContentCookScope scope,
        ContentCookInput sceneInput,
        IReadOnlyDictionary<NodeId, int> nodeIndices,
        Guid operationId,
        List<DiagnosticRecord> diagnostics)
    {
        var bindings = new List<ScriptSidecarBinding>();
        var sites = new List<ScriptBindingSite>();
        var references = new List<Uri>();
        foreach (var node in scene.AllNodes.Where(node => nodeIndices.ContainsKey(node.Id)).OrderBy(node => nodeIndices[node.Id]))
        {
            var slots = node.Components.OfType<ScriptingComponent>().SelectMany(static scripting => scripting.Slots).ToArray();
            for (var number = 1; number <= slots.Length; number++)
            {
                var slot = slots[number - 1];
                var site = new ScriptBindingSite(node.Id, node.Name, slot.Id, number, [.. slot.Overrides.Keys]);
                ScriptBindingSite At(string? parameter, DiagnosticSeverity severity, string code, string message)
                {
                    diagnostics.Add(ScriptSlotDiagnostics.Create(operationId, severity, code, message, sceneInput, scene.Id, scene.Name, site, parameter));
                    return site;
                }

                if (slot.Script is not { } script)
                {
                    _ = At(parameter: null, DiagnosticSeverity.Warning, ScriptDiagnosticCodes.SlotWithoutScript, $"{Capitalize(ScriptSlotDiagnostics.Describe(site))} has no script, so it is left out of the cooked scene.");
                    continue;
                }

                var scriptPath = Uri.UnescapeDataString(script.AbsolutePath);
                if (!scriptPath.EndsWith(AssetFileTypes.Script, StringComparison.OrdinalIgnoreCase)
                    || (CookInputResolver.IsAuthoringUri(scope.Project, script) && CookInputResolver.TryResolveScript(scope.Project, script, ContentCookInputRole.Dependency) is null))
                {
                    _ = At(parameter: null, DiagnosticSeverity.Error, ScriptDiagnosticCodes.SlotScriptMissing, $"{Capitalize(ScriptSlotDiagnostics.Describe(site))} runs '{scriptPath}', which no longer exists. Pick another script.");
                    continue;
                }

                references.Add(script);
                var parameters = new List<ScriptSidecarParam>();
                foreach (var (name, value) in slot.Overrides)
                {
                    object? cooked = value.Kind switch
                    {
                        ScriptValueKind.BoolValue => value.BoolValue,
                        ScriptValueKind.Int32Value => value.IntValue,
                        ScriptValueKind.FloatValue => value.FloatValue,
                        ScriptValueKind.StringValue => value.StringValue,
                        ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value => value.GetVectorComponents(),
                        ScriptValueKind.AssetValue => ToAssetVirtualPath(value.AssetUri!),
                        ScriptValueKind.NodeValue => nodeIndices.TryGetValue(value.NodeId, out var index) ? (uint)index : null,
                        _ => null,
                    };
                    if (cooked is null)
                    {
                        _ = At(name, DiagnosticSeverity.Error, ScriptDiagnosticCodes.SlotNodeMissing, $"Parameter '{name}' of {ScriptSlotDiagnostics.Describe(site)} refers to a node the scene no longer has. Pick another node or clear it.");
                        continue;
                    }

                    if (value.Kind == ScriptValueKind.AssetValue)
                    {
                        references.Add(value.AssetUri!);
                    }

                    parameters.Add(new ScriptSidecarParam(name, ScriptingComponent.GetTypeName(value.Kind), cooked));
                }

                bindings.Add(new ScriptSidecarBinding((uint)nodeIndices[node.Id], slot.Id.ToString("D"), scriptPath, slot.ExecutionOrder, parameters));
                sites.Add(site with { ParameterNames = [.. parameters.Select(static parameter => parameter.Key)] });
            }
        }

        return (bindings, sites, [.. references.Distinct()]);
    }

    private static string ToAssetVirtualPath(Uri asset)
    {
        var path = Uri.UnescapeDataString(asset.AbsolutePath);
        return path.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? path[..^".json".Length] : path;
    }

    private static string Capitalize(string text) => text.Length == 0 ? text : char.ToUpperInvariant(text[0]) + text[1..];
}
