// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Inserts node subtrees, each at its place in the graph and in the explorer layout.</summary>
/// <remarks>
/// The node and component ids are in the data, chosen by the edit's builder, so the caller already holds them. The
/// places all refer to the scene before the edit; inserted data obeys the component rules, so an insert can never
/// create a second atmosphere sun. References to moved assets follow the asset.
/// </remarks>
public sealed record InsertNodes : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="InsertNodes"/> class.</summary>
    /// <param name="inserts">The subtrees and their places.</param>
    /// <param name="label">The edit's name in the history; defaults to one naming the inserted nodes.</param>
    public InsertNodes(IReadOnlyList<NodeInsert> inserts, string? label = null)
    {
        ArgumentNullException.ThrowIfNull(inserts);
        this.Inserts = [.. inserts];
        this.Label = label ?? (this.Inserts.Count == 1 ? $"Create {this.Inserts[0].Node.Name}" : $"Create {this.Inserts.Count} nodes");
    }

    /// <summary>Gets the subtrees and their places.</summary>
    public IReadOnlyList<NodeInsert> Inserts { get; }

    /// <inheritdoc/>
    public string Label { get; }

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Inserts.Select(static insert => insert.At.Parent).OfType<NodeId>().Distinct()];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var error = this.Check(scope);
        return error is null ? SceneEditValidation.Valid : SceneEditValidation.Invalid(error);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var scene = scope.Scene;
        var layout = scope.Layout;
        var nodes = this.Inserts.Select(insert => (Insert: insert, Node: SceneNode.CreateAndHydrate(scene, SerializationCopies.Copy(insert.Node)))).ToArray();
        foreach (var (insert, node) in nodes.OrderBy(static pair => pair.Insert.At.Index))
        {
            SceneGraph.Attach(scene, node, insert.At.Parent is { } parent ? scope.FindNode(parent) : null, insert.At.Index);
            _ = SceneReferenceRetargeting.RetargetSubtree(node, scope.Moved);
            foreach (var held in node.DescendantsAndSelf())
            {
                scope.WarnIfDeleted(SceneAssetReferences.Runtime(held).Concat(SceneAssetReferences.Instances(held)).Concat(SceneAssetReferences.Scripts(held)), held.Id);
            }
        }

        foreach (var (insert, node) in nodes.OrderBy(static pair => pair.Insert.Layout.Index))
        {
            var entry = insert.LayoutEntry is { } captured ? SerializationCopies.Copy(captured) : SceneLayout.EntryFor(node);
            SceneLayout.RequireContainer(layout, insert.Layout).Insert(insert.Layout.Index, entry);
        }

        NodeId[] roots = [.. nodes.Select(static pair => pair.Node.Id)];
        return new SceneEditApplication.Applied(new RemoveNodes(roots), [new SceneChange.NodesAdded(roots), new SceneChange.LayoutChanged()]);
    }

    private SceneDiagnostic? Check(SceneEditScope scope)
    {
        if (this.Inserts.Count == 0)
        {
            return SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to insert.");
        }

        var nodeIds = scope.Scene.AllNodes.Select(static node => node.Id).ToHashSet();
        var componentIds = scope.Scene.AllNodes.SelectMany(static node => node.Components).Select(static component => component.Id).ToHashSet();
        var layout = scope.Layout;
        foreach (var insert in this.Inserts)
        {
            var (nodes, components) = SceneGraph.IdsOf(insert.Node);
            if (nodes.Any(id => id.IsEmpty || !nodeIds.Add(id)))
            {
                return SceneDiagnostic.Error(SceneEditCodes.NodeExists, "An inserted node has an identity already in the scene.");
            }

            if (components.Any(id => !componentIds.Add(id)))
            {
                return SceneDiagnostic.Error(SceneEditCodes.ComponentDuplicate, "An inserted component has an identity already in the scene.");
            }

            if ((SceneGraph.Check(scope, insert.At) ?? SceneLayout.Check(layout, insert.Layout)) is { } place)
            {
                return place;
            }

            var (_, layoutScope) = SceneLayout.ScopeOf(layout, insert.Layout);
            if (layoutScope != insert.At.Parent)
            {
                return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "An inserted node's explorer place is not in the scope of its scene parent.");
            }
        }

        return CheckSuns(scope.Scene, this.Inserts.Select(static insert => insert.Node));
    }

    // One atmosphere slot has at most one light: inserted lights may not take a slot the scene, or another insert, has.
    private static SceneDiagnostic? CheckSuns(Scene scene, IEnumerable<SceneNodeData> inserted)
    {
        var owners = scene.AllNodes
            .SelectMany(static node => node.Components.OfType<DirectionalLightComponent>().Select(light => (light.AtmosphereSlot, node.Metadata.Name)))
            .Where(static owner => owner.AtmosphereSlot != AtmosphereLightSlot.None)
            .ToDictionary(static owner => owner.AtmosphereSlot, static owner => owner.Name);
        foreach (var (slot, name) in inserted.SelectMany(Lights))
        {
            if (slot != AtmosphereLightSlot.None && !owners.TryAdd(slot, name))
            {
                return SceneDiagnostic.Error(SceneEditCodes.AtmosphereSlotOccupied, $"Atmosphere slot {slot} is already owned by '{owners[slot]}'.");
            }
        }

        return null;

        static IEnumerable<(AtmosphereLightSlot Slot, string Name)> Lights(SceneNodeData node)
            => node.Components.OfType<DirectionalLightData>().Select(light => (light.AtmosphereSlot, node.Name))
                .Concat((node.Children ?? []).SelectMany(Lights));
    }
}
