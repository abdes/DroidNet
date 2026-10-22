// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scripting;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
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
    private static (List<ScriptSidecarBinding> Bindings, List<ScriptBindingSite> Sites, List<AssetPath> References) CreateScriptBindings(
        Scene scene,
        ContentCookScope scope,
        ContentCookInput sceneInput,
        IReadOnlyDictionary<NodeId, int> nodeIndices,
        Guid operationId,
        List<DiagnosticRecord> diagnostics)
    {
        var bindings = new List<ScriptSidecarBinding>();
        var sites = new List<ScriptBindingSite>();
        var references = new List<AssetPath>();
        foreach (var node in scene.AllNodes.Where(node => nodeIndices.ContainsKey(node.Id)).OrderBy(node => nodeIndices[node.Id]))
        {
            var slots = node.Components.OfType<ScriptingComponent>().SelectMany(static scripting => scripting.Slots).ToArray();
            for (var number = 1; number <= slots.Length; number++)
            {
                var slot = slots[number - 1];
                var site = new ScriptBindingSite(node.Id, node.Metadata.Name, slot.Id, number, [.. slot.Overrides.Keys]);
                var problems = ScriptSlotRules.Check(
                    slot,
                    nodeIndices.ContainsKey,
                    path => !CookInputResolver.IsAuthoringUri(scope.Project, path) || CookInputResolver.TryResolveScript(scope.Project, path, ContentCookInputRole.Dependency) is not null);
                diagnostics.AddRange(problems.Select(problem => ScriptSlotDiagnostics.Create(operationId, problem, sceneInput, scene.Id, scene.Name, site)));
                if (slot.Script is not { } script || problems.Any(static problem => problem.Field.Parameter is null))
                {
                    continue;
                }

                references.Add(script);
                var parameters = new List<ScriptSidecarParam>();
                foreach (var (name, value) in slot.Overrides.Where(entry => !problems.Any(problem => string.Equals(problem.Field.Parameter, entry.Key, StringComparison.Ordinal))))
                {
                    object? cooked = value.Kind switch
                    {
                        ScriptValueKind.BoolValue => value.BoolValue,
                        ScriptValueKind.Int32Value => value.IntValue,
                        ScriptValueKind.FloatValue => value.FloatValue,
                        ScriptValueKind.StringValue => value.StringValue,
                        ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value => value.GetVectorComponents(),
                        ScriptValueKind.AssetValue => value.Asset!.VirtualPath,
                        ScriptValueKind.NodeValue => nodeIndices.TryGetValue(value.NodeId, out var index) ? (uint)index : null,
                        _ => null,
                    };
                    if (cooked is null)
                    {
                        continue;
                    }

                    if (value.Kind == ScriptValueKind.AssetValue)
                    {
                        references.Add(value.Asset!);
                    }

                    parameters.Add(new ScriptSidecarParam(name, ScriptingComponent.GetTypeName(value.Kind), cooked));
                }

                bindings.Add(new ScriptSidecarBinding((uint)nodeIndices[node.Id], slot.Id.ToString("D"), script.VirtualPath, slot.ExecutionOrder, parameters));
                sites.Add(site with { ParameterNames = [.. parameters.Select(static parameter => parameter.Key)] });
            }
        }

        return (bindings, sites, [.. references.Distinct()]);
    }
}
