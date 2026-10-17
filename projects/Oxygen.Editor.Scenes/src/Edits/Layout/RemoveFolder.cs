// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Removes an explorer folder; removing a folder only regroups, it never removes nodes.</summary>
/// <remarks>
/// With <paramref name="PromoteChildren"/>, the folder's entries take its place in its container, in their order.
/// Without it, the folder must be empty: that form is the revert of <see cref="CreateFolder"/>.
/// </remarks>
/// <param name="Folder">The folder.</param>
/// <param name="PromoteChildren">Whether the folder's entries take its place.</param>
public sealed record RemoveFolder(FolderId Folder, bool PromoteChildren) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Remove Folder";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (scope.CheckScope(SceneLayout.ScopeOfEntry(scope.Scene.ExplorerLayout ?? [], LayoutEntryId.Of(this.Folder)).Scope) is { IsValid: false } refused)
        {
            return refused;
        }

        return SceneLayout.Find(scope.Scene.ExplorerLayout, LayoutEntryId.Of(this.Folder)) is not { } found
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The folder no longer exists."))
            : !this.PromoteChildren && found.Entry.Children is { Count: > 0 }
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderNotEmpty, "The folder is not empty."))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = SceneLayout.Of(scope.Scene);
        var at = SceneLayout.PositionOf(layout, LayoutEntryId.Of(this.Folder))!;
        var (folder, list) = SceneLayout.Find(layout, LayoutEntryId.Of(this.Folder))!.Value;
        var children = folder.Children?.ToArray() ?? [];
        var restored = SerializationCopies.Copy(folder) with { Children = [] };
        list.RemoveAt(at.Index);
        for (var i = 0; i < children.Length; i++)
        {
            list.Insert(at.Index + i, children[i]);
        }

        SceneLayout.Prune(layout);
        return new SceneEditApplication.Applied(
            new RestoreFolder(restored, at, [.. children.Select(SceneLayout.IdOf)]),
            [new SceneChange.LayoutChanged()]);
    }
}
