// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Removes node subtrees, with their explorer entries and the folders under them.</summary>
/// <remarks>A listed node under another listed node is removed with it. A locked node in a subtree refuses the edit.</remarks>
public sealed record RemoveNodes : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RemoveNodes"/> class.</summary>
    /// <param name="roots">The subtrees' roots.</param>
    public RemoveNodes(IReadOnlyList<NodeId> roots)
    {
        ArgumentNullException.ThrowIfNull(roots);
        this.Roots = [.. roots.Distinct()];
    }

    /// <summary>Gets the subtrees' roots.</summary>
    public IReadOnlyList<NodeId> Roots { get; }

    /// <inheritdoc/>
    public string Label => this.Roots.Count == 1 ? "Delete Node" : $"Delete {this.Roots.Count} nodes";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => this.Roots;

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return this.Roots.Count == 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to remove."))
            : scope.CheckSubtrees(this.Roots);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var scene = scope.Scene;
        var layout = scope.Layout;
        var nodes = SceneGraph.TopLevel(scope, this.Roots);

        // Every place is captured before anything moves, so the revert puts them all back exactly.
        var inserts = nodes.Select(node =>
        {
            var entry = SceneLayout.Find(layout, LayoutEntryId.Of(node.Id));
            return new NodeInsert(
                node.Dehydrate(),
                SceneGraph.PositionOf(scene, node),
                SceneLayout.PositionOf(layout, LayoutEntryId.Of(node.Id)) ?? LayoutPosition.AtRoot(0),
                entry?.Entry);
        }).ToArray();
        foreach (var node in nodes)
        {
            if (SceneLayout.Find(layout, LayoutEntryId.Of(node.Id)) is { } entry)
            {
                _ = entry.List.Remove(entry.Entry);
            }

            SceneGraph.Detach(scene, node);
        }

        SceneLayout.Prune(layout);
        NodeId[] roots = [.. nodes.Select(static node => node.Id)];
        return new SceneEditApplication.Applied(
            new InsertNodes(inserts, this.Label.Replace("Delete", "Restore", StringComparison.Ordinal)),
            [new SceneChange.NodesRemoved(roots), new SceneChange.LayoutChanged()]);
    }
}
