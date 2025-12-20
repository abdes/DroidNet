// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using DroidNet.Controls;
using Oxygen.Editor.Core;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Base class for all items in the Scene Explorer layout.
/// Provides robust, DRY management of children (Folders vs Content),
/// expansion state, and UI synchronization.
/// </summary>
public abstract class LayoutItemAdapter : TreeItemAdapter
{
    // Separated collections allow for "Folders First" sorting or distinct UI templates if needed.
    protected readonly ObservableCollection<FolderAdapter> Folders = [];
    protected readonly ObservableCollection<ITreeItem> Content = [];

    protected LayoutItemAdapter()
    {
        // Subscribe to changes to keep the base TreeItemAdapter in sync
        this.Folders.CollectionChanged += this.OnChildrenChanged;
        this.Content.CollectionChanged += this.OnChildrenChanged;
    }

    /// <summary>
    /// Gets a value indicating whether this item has any children (Folders or Content).
    /// </summary>
    public bool HasChildren => this.Folders.Count > 0 || this.Content.Count > 0;

    /// <summary>
    /// Gets the current children (Folders and Content) synchronously.
    /// </summary>
    public IEnumerable<ITreeItem> CurrentChildren => this.Folders.Cast<ITreeItem>().Concat(this.Content);

    /// <summary>
    /// Gets standardized Glyph logic: Open Folder if expanded/has items, else generic icon.
    /// Subclasses can override <see cref="DefaultIconGlyph"/> for the "leaf" state.
    /// </summary>
    public string IconGlyph => this.HasChildren && this.IsExpanded ? "\uE838" : this.DefaultIconGlyph;

    /// <summary>
    /// Gets the icon to display when the item is collapsed or empty.
    /// </summary>
    protected virtual string DefaultIconGlyph => "\uE8B7"; // Default Folder Closed

    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <summary>
    /// Adds a folder to the layout.
    /// </summary>
    public void AddFolder(FolderAdapter folder)
    {
        if (!this.Folders.Contains(folder))
        {
            this.Folders.Add(folder);
        }
    }

    /// <summary>
    /// Adds a content item (Node) to the layout.
    /// </summary>
    public void AddContent(ITreeItem item)
    {
        if (!this.Content.Contains(item))
        {
            this.Content.Add(item);
        }
    }

    /// <summary>
    /// Removes a folder from the layout.
    /// </summary>
    public bool RemoveFolder(FolderAdapter folder) => this.Folders.Remove(folder);

    /// <summary>
    /// Removes a content item from the layout.
    /// </summary>
    public bool RemoveContent(ITreeItem item) => this.Content.Remove(item);

    protected override int DoGetChildrenCount() => this.Folders.Count + this.Content.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        // Enforce "Folders First" order in the UI
        foreach (var folder in this.Folders)
        {
            this.AddChildInternal(folder);
        }

        foreach (var item in this.Content)
        {
            if (item is TreeItemAdapter adapter)
            {
                this.AddChildInternal(adapter);
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (string.Equals(e.PropertyName, nameof(this.IsExpanded), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
            this.OnIsExpandedChanged(this.IsExpanded);
        }
    }

    /// <summary>
    /// Hook for subclasses to react to expansion changes (e.g. updating model state).
    /// </summary>
    protected virtual void OnIsExpandedChanged(bool isExpanded)
    {
    }

    private void OnChildrenChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        this.OnPropertyChanged(nameof(this.IconGlyph));
        this.OnPropertyChanged(nameof(this.HasChildren));

        // If we are expanded, we must keep the base class's internal collection in sync
        // to ensure the UI updates immediately without requiring a full reload.
        if (this.IsExpanded)
        {
            this.MaterializeChildren(e);
        }

        // Auto-collapse if empty to ensure correct glyph state
        if (!this.HasChildren && this.IsExpanded)
        {
            this.IsExpanded = false;
        }
    }

    private void MaterializeChildren(NotifyCollectionChangedEventArgs e)
    {
        switch (e.Action)
        {
            case NotifyCollectionChangedAction.Add:
                if (e.NewItems != null)
                {
                    foreach (var item in e.NewItems.OfType<TreeItemAdapter>())
                    {
                        // Fire-and-forget add. Safe because AddChildInternal checks for duplicates?
                        // Actually, we should use the Async helper to ensure initialization.
                        _ = this.SafeAddChildAsync(item);
                    }
                }

                break;

            case NotifyCollectionChangedAction.Remove:
                if (e.OldItems != null)
                {
                    foreach (var item in e.OldItems.OfType<TreeItemAdapter>())
                    {
                        // IMPORTANT: When removing a child, we must ensure it is deselected first.
                        // If the UI still thinks it's selected, it will keep focus and cause weird state.
                        item.IsSelected = false;
                        _ = this.SafeRemoveChildAsync(item);
                    }
                }

                break;

            case NotifyCollectionChangedAction.Reset:
                // Full reload required
                _ = this.LoadChildren();
                break;
        }
    }

    private async Task SafeAddChildAsync(TreeItemAdapter child)
    {
        try
        {
            // Ensure Children collection is realized
            var children = await this.Children.ConfigureAwait(false);
            if (!children.Contains(child))
            {
                this.AddChildInternal(child);
            }
        }
        catch (Exception)
        {
            // Swallow to avoid crashing background sync
        }
    }

    private async Task SafeRemoveChildAsync(TreeItemAdapter child)
    {
        try
        {
            await this.Children.ConfigureAwait(false);
            await this.RemoveChildAsync(child).ConfigureAwait(false);
        }
        catch (Exception)
        {
            // Swallow to avoid crashing background sync
        }
    }
}
