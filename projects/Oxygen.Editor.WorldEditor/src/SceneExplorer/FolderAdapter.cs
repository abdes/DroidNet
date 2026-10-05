// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Editor-only tree adapter representing a folder/grouping inside the Scene Explorer.
/// Folders only exist in the explorer UI and reference nodes by adapter objects — they
/// do not correspond to SceneNode instances in the scene graph.
/// </summary>
public sealed class FolderAdapter : LayoutItemAdapter, ICanBeCloned
{
    /// <summary>The display name used for folder entries without an authored name.</summary>
    internal const string FallbackName = "Folder";

    private readonly ExplorerEntryData? entryData;
    private bool isExpansionTransient;

    public FolderAdapter(ExplorerEntryData entry)
        : this(entry.FolderId ?? Guid.NewGuid(), entry.Name ?? FallbackName)
    {
        this.entryData = entry;
        this.IsExpanded = entry.IsExpanded ?? false;
    }

    public FolderAdapter(Guid id, string name)
    {
        this.Id = id;
        this.Name = name;
    }

    public Guid Id { get; }

    public string Name
    {
        get;
        set
        {
            if (string.Equals(value, field, StringComparison.Ordinal))
            {
                return;
            }

            field = value;
            this.OnPropertyChanged(nameof(this.Label));
        }
    }

    public override string Label
    {
        get => this.Name;
        set => throw new NotSupportedException("Folder name is read-only presentation; rename through the document command owner.");
    }

    /// <summary>Gets a value indicating whether expansion is view-only state.</summary>
    internal bool IsExpansionTransient => this.isExpansionTransient;

    /// <inheritdoc />
    public ITreeItem CloneSelf()
    {
        // Return a new folder with a new ID but same name.
        return new FolderAdapter(Guid.NewGuid(), this.Name);
    }

    /// <summary>
    ///     Controls whether expansion changes are persisted to the authored layout entry. Search
    ///     sets this to <see langword="true"/> so its transient ancestor expansion does not write
    ///     <see cref="ExplorerEntryData.IsExpanded"/> or dirty the document.
    /// </summary>
    /// <param name="transient"><see langword="true"/> to suppress authored expansion persistence.</param>
    public void SetExpansionTransient(bool transient) => this.isExpansionTransient = transient;

    protected override void OnIsExpandedChanged(bool isExpanded)
    {
        if (this.entryData is not null && !this.isExpansionTransient)
        {
            this.entryData.IsExpanded = isExpanded;
        }
    }
}
