// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Puts nodes back at placements captured from one scene: graph place, local transform and layout place.</summary>
/// <remarks>The placements all refer to the same scene, so the nodes return exactly where they were.</remarks>
public sealed record PlaceNodes : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="PlaceNodes"/> class.</summary>
    /// <param name="placements">The nodes and their placements.</param>
    public PlaceNodes(IReadOnlyList<NodePlacementState> placements)
    {
        ArgumentNullException.ThrowIfNull(placements);
        this.Placements = [.. placements];
    }

    /// <summary>Gets the nodes and their placements.</summary>
    public IReadOnlyList<NodePlacementState> Placements { get; }

    /// <inheritdoc/>
    public string Label => this.Placements.Count == 1 ? "Move Node" : $"Move {this.Placements.Count} nodes";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Placements.Select(static placement => placement.Node)];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var moved = this.Placements.Select(static placement => placement.Node).ToHashSet();
        var layout = scope.Scene.ExplorerLayout ?? [];
        foreach (var placement in this.Placements)
        {
            if (scope.FindNode(placement.Node) is not { } node)
            {
                return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The node no longer exists.", placement.Node));
            }

            if (placement.At.Parent is { } parentId)
            {
                if (scope.FindNode(parentId) is not { } parent || moved.Contains(parentId))
                {
                    return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The node's parent is no longer where it was.", parentId));
                }

                if (ReferenceEquals(parent, node) || parent.Ancestors().Contains(node))
                {
                    return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "A node cannot be moved into its own descendant.", placement.Node));
                }
            }

            var (found, _) = SceneLayout.ScopeOf(layout, placement.Layout);
            if (!found)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The node's explorer place is no longer in the layout.", placement.Node));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var scene = scope.Scene;
        var layout = SceneLayout.Of(scene);
        var nodes = this.Placements.Select(placement => (Placement: placement, Node: scope.FindNode(placement.Node)!)).ToArray();
        var before = nodes.Select(pair => SceneGraph.PlacementOf(scene, pair.Node)).ToArray();
        var entries = nodes.Select(pair => SceneLayout.Find(layout, LayoutEntryId.Of(pair.Node.Id))?.Entry ?? SceneLayout.EntryFor(pair.Node)).ToArray();

        SceneGraph.Place(scene, [.. nodes.Select(static pair => (pair.Node, pair.Placement.At))]);
        foreach (var (placement, node) in nodes)
        {
            SceneGraph.SetLocal(node, placement.Local);
        }

        SceneLayout.Place(layout, [.. nodes.Select((pair, i) => (entries[i], pair.Placement.Layout))]);
        NodeId[] moved = [.. nodes.Select(static pair => pair.Node.Id)];

        SceneLayout.Prune(layout);
        return new SceneEditApplication.Applied(new PlaceNodes(before), [new SceneChange.NodesPlaced(moved), new SceneChange.LayoutChanged()]);
    }
}
