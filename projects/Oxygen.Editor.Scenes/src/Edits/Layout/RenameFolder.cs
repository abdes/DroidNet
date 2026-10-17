// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Renames an explorer folder.</summary>
/// <param name="Folder">The folder.</param>
/// <param name="Name">The new name.</param>
public sealed record RenameFolder(FolderId Folder, string Name) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => $"Rename Folder to '{this.Name}'";

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

        return string.IsNullOrWhiteSpace(this.Name)
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "Folder names cannot be empty."))
            : SceneLayout.Find(scope.Scene.ExplorerLayout, LayoutEntryId.Of(this.Folder)) is null
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The folder no longer exists."))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var folder = SceneLayout.Find(scope.Scene.ExplorerLayout, LayoutEntryId.Of(this.Folder))!.Value.Entry;
        var name = this.Name.Trim();
        var before = folder.Name ?? string.Empty;
        if (string.Equals(before, name, StringComparison.Ordinal))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        folder.Name = name;
        return new SceneEditApplication.Applied(this with { Name = before }, [new SceneChange.LayoutChanged()]);
    }
}
