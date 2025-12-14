// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Editor-only tree adapter representing a folder/grouping inside the Scene Explorer.
/// Folders only exist in the explorer UI and reference nodes by adapter objects — they
/// do not correspond to SceneNode instances in the scene graph.
/// </summary>
public sealed class FolderAdapter : TreeItemAdapter, ICanBeCloned
{
    private readonly ObservableCollection<ITreeItem> children = new();
    internal IEnumerable<ITreeItem> InternalChildren => this.children;
    private readonly ExplorerEntryData? entryData;

    public FolderAdapter(ExplorerEntryData entry)
        : this(entry.FolderId ?? Guid.NewGuid(), entry.Name ?? "Folder")
    {
        this.entryData = entry;
        this.IsExpanded = entry.IsExpanded ?? false;

        // Keep the underlying data model in sync with the UI state so that layout serialization
        // and restoration (e.g. during Undo/Redo) preserves the expansion state.
        this.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(this.IsExpanded))
            {
                this.entryData.IsExpanded = this.IsExpanded;
            }
        };
    }

    public FolderAdapter(Guid id, string name)
    {
        this.Id = id;
        this.Name = name;
        // Keep IconGlyph up to date when children change
        this.children.CollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
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

    protected override int DoGetChildrenCount() => this.children.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        foreach (var child in this.children)
        {
            if (child is TreeItemAdapter adapter)
            {
                this.AddChildInternal(adapter);
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    public void AddChildAdapter(ITreeItem child)
    {
        if (child is TreeItemAdapter adapter)
        {
            this.children.Add(adapter);
            if (this.IsExpanded)
            {
                // Ensure the underlying children collection is initialized before adding to it
                // We use fire-and-forget here because we can't block, but we want to ensure
                // the child is added if the collection is realized.
                _ = this.AddChildToInternalCollectionAsync(adapter);
            }
        }
    }

    private async Task AddChildToInternalCollectionAsync(TreeItemAdapter adapter)
    {
        var realizedChildren = await this.Children.ConfigureAwait(false);
        if (!realizedChildren.Contains(adapter))
        {
            this.AddChildInternal(adapter);
        }
    }

    /// <inheritdoc />
    public ITreeItem CloneSelf()
    {
        // Return a new folder with a new ID but same name.
        return new FolderAdapter(Guid.NewGuid(), this.Name);
    }


    public bool RemoveChildAdapter(ITreeItem child)
    {
        var removed = this.children.Remove(child);
        if (removed && child is TreeItemAdapter adapter)
        {
            // Ensure children collection has been initialized, then remove the
            // adapter from the base class children collection. Use RemoveChildAsync
            // synchronously here because this helper is synchronous.
            _ = this.Children.ConfigureAwait(false);
            _ = this.RemoveChildAsync(adapter).GetAwaiter().GetResult();
        }

        return removed;
    }

    /// <summary>
    /// Expose the underlying child adapters as a read-only list for layout updates.
    /// </summary>
    public IReadOnlyList<ITreeItem> ChildAdapters => this.children.ToList();

    /// <summary>
    /// Glyph for showing open/closed folder icon in thumbnail.
    /// Mirrors ProjectExplorer's FolderGlyph behavior (open when expanded and has children).
    /// </summary>
    public string IconGlyph => this.IsExpanded && this.children.Count > 0 ? "\uE838" : "\uE8B7";

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (e.PropertyName?.Equals(nameof(this.IsExpanded), StringComparison.Ordinal) == true)
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
            if (this.entryData is not null)
            {
                this.entryData.IsExpanded = this.IsExpanded;
            }
        }
    }
}
