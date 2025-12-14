// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Adapter that wraps a <see cref="SceneNode" /> payload for the Scene Explorer.
/// This adapter supports the "Overlay" layout concept where the visual hierarchy
/// (folders, reordering) can differ from the scene graph.
/// </summary>
public sealed class SceneNodeAdapter : TreeItemAdapter, ITreeItem<SceneNode>, ICanBeCloned
{
    private readonly SceneNode payload;

    // Layout-owned children (folders and node adapters). This is the authoritative
    // children list for the explorer view.
    private readonly ObservableCollection<ITreeItem> layoutChildren = new();

    /// <summary>
    /// Gets the list of children managed by the layout (folders and other nodes).
    /// </summary>
    public ObservableCollection<ITreeItem> LayoutChildren => this.layoutChildren;

    public SceneNodeAdapter(SceneNode payload)
    {
        this.payload = payload ?? throw new ArgumentNullException(nameof(payload));
        this.IsExpanded = this.payload.IsExpanded;
        this.payload.PropertyChanged += this.OnPayloadPropertyChanged;

        // Keep icon/label reactive when layout children change
        this.layoutChildren.CollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <summary>
    /// Gets the wrapped scene node payload.
    /// </summary>
    public SceneNode AttachedObject => this.payload;

    public override string Label
    {
        get => this.payload.Name;
        set
        {
            if (string.Equals(value, this.payload.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.payload.Name = value;
            this.OnPropertyChanged();
        }
    }

    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <summary>
    /// Glyph used for thumbnails or icons.
    /// </summary>
    public string IconGlyph
    {
        get
        {
            // TODO: Use component-based icons (Light, Camera, etc.)
            if (this.layoutChildren.Count > 0)
            {
                return this.IsExpanded ? "\uE838" : "\uE8B7";
            }

            // Default node glyph
            return "\uE7C1";
        }
    }

    /// <summary>
    /// Add a layout child to this adapter.
    /// </summary>
    public void AddLayoutChild(ITreeItem child)
    {
        if (child is TreeItemAdapter adapter)
        {
            this.layoutChildren.Add(adapter);
            if (this.IsExpanded)
            {
                // If we are expanded, we need to ensure the child is added to the internal children collection
                // We can't easily check if it's already there without awaiting, so we rely on the fact that
                // LoadChildren clears and rebuilds, or we could try to add it if initialized.
                // For simplicity and safety with the lazy loading pattern, we can trigger a refresh or just add it.
                // Accessing Children property triggers loading if not loaded.

                // Ideally, we should just add it to layoutChildren and let the TreeItemAdapter mechanism handle it
                // but TreeItemAdapter doesn't observe layoutChildren.
                // So we need to manually update the internal children if they are already loaded.

                // NOTE: This logic mimics LayoutNodeAdapter's behavior.
                // We can't synchronously check if Children is realized.
                // But we can check if the task is completed.
                // For now, let's just add it to layoutChildren. The UI will trigger LoadChildren if expanded.
                // If already expanded and loaded, we might need to force an update.

                // A safer bet is to call AddChildInternal if we know we are loaded.
                // But TreeItemAdapter doesn't expose "IsLoaded".

                // Let's stick to what LayoutNodeAdapter did, but be careful with async.
                // LayoutNodeAdapter did:
                // var realizedChildren = this.Children.ConfigureAwait(false).GetAwaiter().GetResult();
                // if (!realizedChildren.Contains(adapter)) { this.AddChildInternal(adapter); }
                // This blocks! But maybe it's cached so it's fast.

                 _ = this.AddChildToInternalCollectionAsync(adapter);
            }
        }
        else
        {
            this.layoutChildren.Add(child);
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

    public bool RemoveLayoutChild(ITreeItem child)
    {
        if (this.layoutChildren.Remove(child))
        {
            if (child is TreeItemAdapter adapter)
            {
                this.RemoveChildInternal(adapter);
            }
            return true;
        }
        return false;
    }

    protected override int DoGetChildrenCount() => this.layoutChildren.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        foreach (var child in this.layoutChildren)
        {
            if (child is TreeItemAdapter adapter)
            {
                this.AddChildInternal(adapter);
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private void OnPayloadPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(SceneNode.Name))
        {
            this.OnPropertyChanged(nameof(this.Label));
        }
        else if (e.PropertyName == nameof(SceneNode.IsExpanded))
        {
            this.IsExpanded = this.payload.IsExpanded;
        }
    }

    /// <inheritdoc />
    public ITreeItem CloneSelf()
    {
        // Create a new adapter with the same payload (SceneNode).
        // Note: This does NOT clone the SceneNode itself. The SceneNode is the model.
        // If the intention of CloneSelf in the context of Copy/Paste is to eventually create a COPY of the node,
        // then we might need to handle that differently.
        // However, the Redesign says:
        // "Cloning: CloneSelf() returns a new adapter with the same properties but no connections."
        // And "Scene Logic: When a SceneNodeAdapter is pasted, the SceneExplorerService must be notified to create the corresponding SceneNode"

        // So here we just return a new Adapter. But wait, if we paste it, we want a NEW SceneNode eventually.
        // If we just wrap the same SceneNode, we have two adapters pointing to the same node.
        // The Service will handle the actual duplication of the SceneNode.
        // So for the "Clipboard" representation, we might need to carry the data needed to clone.

        // If I return `new SceneNodeAdapter(this.payload)`, I am sharing the model.
        // When `Paste` happens, the Service will take this Adapter, see it's a SceneNodeAdapter,
        // and probably ask the Scene to clone the Node.

        return new SceneNodeAdapter(this.payload);
    }
}
