// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Creates an empty explorer folder at a place in the layout.</summary>
/// <param name="Folder">The new folder's id, chosen by the caller.</param>
/// <param name="At">Where the folder goes.</param>
/// <param name="Name">The folder's name.</param>
/// <param name="IsExpanded">Whether the explorer shows the folder expanded, or <see langword="null"/> for its default.</param>
public sealed record CreateFolder(FolderId Folder, LayoutPosition At, string Name, bool? IsExpanded = null) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => $"Create Folder '{this.Name}'";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => this.At.Node is { } node ? [node] : [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        if (scope.CheckScope(SceneLayout.ScopeOf(layout, this.At).Scope) is { IsValid: false } refused)
        {
            return refused;
        }

        var error = string.IsNullOrWhiteSpace(this.Name) ? SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "Folder names cannot be empty.")
            : this.Folder.IsEmpty || SceneLayout.Find(layout, LayoutEntryId.Of(this.Folder)) is not null ? SceneDiagnostic.Error(SceneEditCodes.FolderExists, "The layout already has a folder with this identity.")
            : SceneLayout.Check(layout, this.At);
        return error is null ? SceneEditValidation.Valid : SceneEditValidation.Invalid(error);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        SceneLayout.RequireContainer(layout, this.At).Insert(
            this.At.Index,
            new ExplorerEntryData { Type = SceneLayout.FolderType, FolderId = this.Folder, Name = this.Name.Trim(), IsExpanded = this.IsExpanded, Children = [] });
        return new SceneEditApplication.Applied(new RemoveFolder(this.Folder, PromoteChildren: false), [new SceneChange.LayoutChanged()]);
    }
}
