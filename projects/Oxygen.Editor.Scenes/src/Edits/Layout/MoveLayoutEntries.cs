// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Moves explorer entries, in order, to one place in the same scene scope.</summary>
/// <remarks>
/// Moving changes only the grouping, so the destination must be in the scope of each moved node's scene parent, and of
/// the scene parents of the nodes a moved folder holds; an empty folder fits any scope. Moving into another scope is a
/// batch that reparents first. The index is taken after the moved entries left their places.
/// </remarks>
public sealed record MoveLayoutEntries : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="MoveLayoutEntries"/> class.</summary>
    /// <param name="entries">The entries, in the order they take at the destination.</param>
    /// <param name="to">The destination.</param>
    public MoveLayoutEntries(IReadOnlyList<LayoutEntryId> entries, LayoutPosition to)
    {
        ArgumentNullException.ThrowIfNull(entries);
        ArgumentNullException.ThrowIfNull(to);
        this.Entries = [.. entries.Distinct()];
        this.To = to;
    }

    /// <summary>Gets the entries, in the order they take at the destination.</summary>
    public IReadOnlyList<LayoutEntryId> Entries { get; }

    /// <summary>Gets the destination.</summary>
    public LayoutPosition To { get; }

    /// <inheritdoc/>
    public string Label => "Move in Explorer";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        if (scope.CheckScope(SceneLayout.ScopeOf(layout, this.To).Scope) is { IsValid: false } refused)
        {
            return refused;
        }

        var error = this.Check(scope, layout);
        return error is null ? SceneEditValidation.Valid : SceneEditValidation.Invalid(error);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        var before = this.Entries.Select(id => new LayoutEntryPosition(id, SceneLayout.PositionOf(layout, id)!)).ToArray();
        var moved = this.Entries.Select(id => SceneLayout.Find(layout, id)!.Value).ToArray();
        foreach (var (entry, list) in moved)
        {
            _ = list.Remove(entry);
        }

        var container = SceneLayout.RequireContainer(layout, this.To);
        for (var i = 0; i < moved.Length; i++)
        {
            container.Insert(this.To.Index + i, moved[i].Entry);
        }

        SceneLayout.Prune(layout);
        var after = this.Entries.Select(id => new LayoutEntryPosition(id, SceneLayout.PositionOf(layout, id)!));
        return before.SequenceEqual(after)
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new PlaceLayoutEntries(before), [new SceneChange.LayoutChanged()]);
    }

    // The scene parent decides a node's scope, not where its entry is: in a batch, a reparent runs before the move.
    private static bool FitsScope(SceneEditScope scope, ExplorerEntryData entry, NodeId? destination)
        => (SceneLayout.IsNode(entry) ? [entry.NodeId!.Value] : SceneLayout.ScopeNodes(entry.Children ?? []))
            .Select(scope.FindNode)
            .OfType<SceneNode>()
            .All(node => node.Hierarchy.Parent?.Id == destination);

    private SceneDiagnostic? Check(SceneEditScope scope, IList<ExplorerEntryData> layout)
    {
        if (this.Entries.Count == 0)
        {
            return SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No entry to move.");
        }

        var (found, destinationScope) = SceneLayout.ScopeOf(layout, this.To);
        if (!found)
        {
            return SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The layout destination no longer exists.");
        }

        foreach (var id in this.Entries)
        {
            if (SceneLayout.Find(layout, id) is not { } moved)
            {
                return SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "An entry to move is no longer in the layout.");
            }

            if (!FitsScope(scope, moved.Entry, destinationScope))
            {
                return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "The destination is in another node's scope; reparent the nodes to move them there.");
            }

            if (this.To.Folder is { } folder && (id == LayoutEntryId.Of(folder) || SceneLayout.IsInside(layout, LayoutEntryId.Of(folder), id)))
            {
                return SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "A folder cannot be moved into itself.");
            }

            if (this.Entries.Any(other => other != id && SceneLayout.IsInside(layout, id, other)))
            {
                return SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "An entry and the folder that holds it cannot be moved together.");
            }
        }

        var remaining = SceneLayout.ContainerOf(layout, this.To)!.Count(entry => !this.Entries.Contains(SceneLayout.IdOf(entry)));
        return this.To.Index < 0 || this.To.Index > remaining ? SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The layout position is outside its container.") : null;
    }
}
