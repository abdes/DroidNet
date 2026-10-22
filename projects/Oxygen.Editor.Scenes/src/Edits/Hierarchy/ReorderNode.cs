// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Moves a node to a place among its parent's children and its entry to a place in the explorer layout.</summary>
/// <remarks>
/// The node keeps its local transform. Both places are taken after the node left its own, and the layout place is in
/// the scope of the graph parent.
/// </remarks>
/// <param name="Node">The node.</param>
/// <param name="At">Its new place in the graph.</param>
/// <param name="Layout">Its entry's new place in the layout.</param>
public sealed record ReorderNode(NodeId Node, NodePosition At, LayoutPosition Layout) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Reorder Node";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => this.At.Parent is { } parent ? [this.Node, parent] : [this.Node];

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
        var node = scope.FindNode(this.Node)!;
        var state = SceneGraph.PlacementOf(scene, layout, node);
        SceneGraph.Detach(scene, node);
        SceneGraph.Attach(scene, node, this.At.Parent is { } parent ? scope.FindNode(parent) : null, this.At.Index);

        var entry = SceneLayout.Find(layout, LayoutEntryId.Of(this.Node));
        if (entry is { } found)
        {
            _ = found.List.Remove(found.Entry);
        }

        SceneLayout.RequireContainer(layout, this.Layout).Insert(this.Layout.Index, entry?.Entry ?? SceneLayout.EntryFor(node));

        SceneLayout.Prune(layout);
        var after = SceneGraph.PlacementOf(scene, layout, node);
        return after.At == state.At && after.Layout == state.Layout
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new PlaceNodes([state]), [new SceneChange.NodesPlaced([this.Node]), new SceneChange.LayoutChanged()]);
    }

    private SceneDiagnostic? Check(SceneEditScope scope)
    {
        var node = scope.FindNode(this.Node)!;
        var parent = this.At.Parent is { } parentId ? scope.FindNode(parentId) : null;
        if (parent is not null && (ReferenceEquals(parent, node) || parent.Ancestors().Contains(node)))
        {
            return SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "A node cannot be moved into its own descendant.", this.Node);
        }

        var leaving = ReferenceEquals(node.Hierarchy.Parent, parent) ? 1 : 0;
        if (SceneGraph.Check(scope, this.At, leaving) is { } place)
        {
            return place;
        }

        var layout = scope.Layout;
        var (found, layoutScope) = SceneLayout.ScopeOf(layout, this.Layout);
        if (!found || SceneLayout.ContainerOf(layout, this.Layout) is not { } container)
        {
            return SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The layout destination no longer exists.");
        }

        if (layoutScope != this.At.Parent)
        {
            return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "The explorer place is not in the scope of the node's scene parent.", this.Node);
        }

        var count = container.Count - container.Count(entry => SceneLayout.IsNode(entry) && entry.NodeId == this.Node);
        return this.Layout.Index < 0 || this.Layout.Index > count
            ? SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The layout position is outside its container.")
            : null;
    }
}
