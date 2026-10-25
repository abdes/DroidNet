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
/// Projects the scene's script slots, and the script parameter changes of its instances, into the bindings of its
/// script sidecar job.
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
    /// <returns>The bindings, the instance bindings, the sites of each and the scripts and assets they use.</returns>
    private static ScriptBindingProjection CreateScriptBindings(
        Scene scene,
        ContentCookScope scope,
        ContentCookInput sceneInput,
        IReadOnlyDictionary<NodeId, int> nodeIndices,
        Guid operationId,
        List<DiagnosticRecord> diagnostics)
    {
        var bindings = new List<ScriptSidecarBinding>();
        var sites = new List<ScriptBindingSite>();
        var instanceBindings = new List<ScriptSidecarInstanceBinding>();
        var instanceSites = new List<ScriptBindingSite>();
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
                var parameters = CookParameters(slot.Overrides.Where(entry => !problems.Any(problem => string.Equals(problem.Field.Parameter, entry.Key, StringComparison.Ordinal))));
                bindings.Add(new ScriptSidecarBinding((uint)nodeIndices[node.Id], slot.Id.ToString("D"), script.VirtualPath, slot.ExecutionOrder, parameters));
                sites.Add(site with { ParameterNames = [.. parameters.Select(static parameter => parameter.Key)] });
            }

            // The slots belong to the source: the cooker checks each change against the slot and the parameter it names.
            foreach (var (path, changes) in node.Components.OfType<SceneInstanceComponent>().SelectMany(static instance => instance.Changes.Ordered))
            {
                foreach (var (slot, values) in changes.Parameters)
                {
                    var parameters = CookParameters(values);
                    instanceBindings.Add(new ScriptSidecarInstanceBinding((uint)nodeIndices[node.Id], NodePathOf(path)!, slot.ToString("D"), parameters));
                    instanceSites.Add(new ScriptBindingSite(node.Id, node.Metadata.Name, slot, SlotNumber: 0, [.. parameters.Select(static parameter => parameter.Key)])
                    {
                        Path = path,
                        InstancedNames = changes.Names,
                    });
                }
            }
        }

        return new(bindings, sites, instanceBindings, instanceSites, [.. references.Distinct()]);

        // A node value that names no node of the cooked scene is left out; ScriptSlotRules reports it on a slot.
        List<ScriptSidecarParam> CookParameters(IEnumerable<KeyValuePair<string, ScriptValue>> values)
        {
            var parameters = new List<ScriptSidecarParam>();
            foreach (var (name, value) in values)
            {
                object? cooked = value.Kind switch
                {
                    ScriptValueKind.BoolValue => value.BoolValue,
                    ScriptValueKind.Int32Value => value.IntValue,
                    ScriptValueKind.FloatValue => value.FloatValue,
                    ScriptValueKind.StringValue => value.StringValue,
                    ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value => value.GetVectorComponents(),
                    ScriptValueKind.AssetValue => value.Asset!.VirtualPath,
                    ScriptValueKind.NodeValue => value.Node is { } reference && nodeIndices.TryGetValue(reference.Node, out var index)
                        ? new ScriptSidecarNodeRef((uint)index, NodePathOf(reference.Path))
                        : null,
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

            return parameters;
        }
    }

    /// <summary>The script sidecar projection of a scene.</summary>
    /// <param name="Bindings">The slot bindings.</param>
    /// <param name="Sites">The node and slot of each binding.</param>
    /// <param name="InstanceBindings">The script parameter changes of the instances.</param>
    /// <param name="InstanceSites">The instanced node and slot of each instance binding.</param>
    /// <param name="References">The scripts and assets the bindings use.</param>
    private sealed record ScriptBindingProjection(
        List<ScriptSidecarBinding> Bindings,
        List<ScriptBindingSite> Sites,
        List<ScriptSidecarInstanceBinding> InstanceBindings,
        List<ScriptBindingSite> InstanceSites,
        List<AssetPath> References);
}
