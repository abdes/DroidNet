// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Puts a removed explorer folder back, with the entries it held, as it was.</summary>
/// <remarks>
/// The entries the folder held were promoted to its place when it was removed; they return into the folder, in their
/// order, with their own nested folders and expanded state.
/// </remarks>
public sealed record RestoreFolder : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RestoreFolder"/> class.</summary>
    /// <param name="entry">The folder's entry, with its name and expanded state; its children are not used.</param>
    /// <param name="at">Where the folder was.</param>
    /// <param name="children">The entries the folder held, in order.</param>
    public RestoreFolder(ExplorerEntryData entry, LayoutPosition at, IReadOnlyList<LayoutEntryId> children)
    {
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentNullException.ThrowIfNull(children);
        this.Entry = SerializationCopies.Copy(entry) with { Children = [] };
        this.At = at;
        this.Children = [.. children];
    }

    /// <summary>Gets the folder's entry.</summary>
    public ExplorerEntryData Entry { get; }

    /// <summary>Gets where the folder goes.</summary>
    public LayoutPosition At { get; }

    /// <summary>Gets the entries the folder takes back, in order.</summary>
    public IReadOnlyList<LayoutEntryId> Children { get; }

    /// <inheritdoc/>
    public string Label => "Restore Folder";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        if (scope.CheckScope(SceneLayout.ScopeOf(layout, this.At).Scope) is { IsValid: false } refused)
        {
            return refused;
        }

        if (!SceneLayout.IsFolder(this.Entry) || SceneLayout.Find(layout, SceneLayout.IdOf(this.Entry)) is not null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderExists, "The folder is already in the layout."));
        }

        // The entries wait where the folder's removal put them: in its place, in order.
        var container = SceneLayout.ContainerOf(layout, this.At);
        var inPlace = container is not null
            && this.At.Index + this.Children.Count <= container.Count
            && this.Children.Select((child, i) => SceneLayout.IdOf(container[this.At.Index + i]) == child).All(static same => same);
        return inPlace ? SceneEditValidation.Valid : SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The folder's entries are no longer where it was removed."));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Layout;
        var container = SceneLayout.RequireContainer(layout, this.At);
        var folder = SerializationCopies.Copy(this.Entry) with { Children = [] };
        for (var i = 0; i < this.Children.Count; i++)
        {
            folder.Children!.Add(container[this.At.Index]);
            container.RemoveAt(this.At.Index);
        }

        container.Insert(this.At.Index, folder);
        return new SceneEditApplication.Applied(
            new RemoveFolder(folder.FolderId!.Value, PromoteChildren: this.Children.Count > 0),
            [new SceneChange.LayoutChanged()]);
    }
}
