// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Editor-only tree adapter representing a folder/grouping inside the Scene Explorer.
/// Folders only exist in the explorer UI and reference nodes by adapter objects — they
/// do not correspond to SceneNode instances in the scene graph.
/// </summary>
public sealed class FolderAdapter : LayoutItemAdapter, ICanBeCloned
{
    private readonly ExplorerEntryData? entryData;

    public FolderAdapter(ExplorerEntryData entry)
        : this(entry.FolderId ?? Guid.NewGuid(), entry.Name ?? "Folder")
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

    public string Name { get; set; }

    public override string Label
    {
        get => this.Name;
        set
        {
            if (string.Equals(value, this.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.Name = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    public ITreeItem CloneSelf()
    {
        // Return a new folder with a new ID but same name.
        return new FolderAdapter(Guid.NewGuid(), this.Name);
    }

    protected override void OnIsExpandedChanged(bool isExpanded)
    {
        if (this.entryData is not null)
        {
            this.entryData.IsExpanded = isExpanded;
        }
    }
}
