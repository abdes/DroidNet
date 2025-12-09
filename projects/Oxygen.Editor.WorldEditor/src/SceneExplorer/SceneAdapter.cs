// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="SceneNode" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class SceneAdapter(Scene scene) : TreeItemAdapter, ITreeItem<Scene>
{
    internal bool UseLayoutAdapters { get; init; }

    /// <inheritdoc />
    public override string Label
    {
        get => this.AttachedObject.Name;
        set
        {
            if (string.Equals(value, this.AttachedObject.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.AttachedObject.Name = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    public Scene AttachedObject => scene;

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => scene.RootNodes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        if (this.UseLayoutAdapters)
        {
            await this.LoadLayoutChildrenAsync().ConfigureAwait(false);
            return;
        }

        await this.LoadSceneChildrenAsync().ConfigureAwait(false);
    }

    private async Task LoadLayoutChildrenAsync(HashSet<Guid>? expandedFolderIds = null, bool preserveNodeExpansion = false)
    {
        // Build layout using LayoutNodeAdapter wrappers so layout operations stay scene-agnostic.
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<System.Guid>();

        if (layout is not null && layout.Count > 0)
        {
            async Task BuildFromEntry(ExplorerEntryData entry, object parent)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    var folder = new FolderAdapter(entry);
                    if (expandedFolderIds is not null && entry.FolderId.HasValue && expandedFolderIds.Contains(entry.FolderId.Value))
                    {
                        folder.IsExpanded = true;
                    }
                    else if (entry.IsExpanded == true)
                    {
                        folder.IsExpanded = true;
                    }

                    if (entry.Children is not null)
                    {
                        foreach (var childEntry in entry.Children)
                        {
                            await BuildFromEntry(childEntry, folder).ConfigureAwait(false);
                        }
                    }

                    if (parent is FolderAdapter folderParent)
                    {
                        folderParent.AddChildAdapter(folder);
                    }
                    else if (parent is SceneAdapter sceneParent)
                    {
                        sceneParent.AddChildInternal(folder);
                    }
                    else if (parent is LayoutNodeAdapter layoutParent)
                    {
                        layoutParent.AddLayoutChild(folder);
                    }

                    return;
                }

                if (!string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) || entry.NodeId is null)
                {
                    return;
                }

                var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
                if (node is null)
                {
                    return;
                }

                var layoutNode = new LayoutNodeAdapter(node);

                if (!preserveNodeExpansion && entry.IsExpanded.HasValue)
                {
                    layoutNode.IsExpanded = entry.IsExpanded.Value;
                }

                _ = seenNodeIds.Add(node.Id);

                // Recurse into declared layout children and add them to the layoutNode
                if (entry.Children is not null)
                {
                    foreach (var childEntry in entry.Children)
                    {
                        await BuildFromEntry(childEntry, layoutNode).ConfigureAwait(false);
                    }
                }

                if (parent is FolderAdapter folderContainer)
                {
                    folderContainer.AddChildAdapter(layoutNode);
                    return;
                }

                if (parent is SceneAdapter sceneParentRoot)
                {
                    sceneParentRoot.AddChildInternal(layoutNode);
                }

                if (parent is LayoutNodeAdapter layoutParentNode)
                {
                    layoutParentNode.AddLayoutChild(layoutNode);
                }
            }

            foreach (var entry in layout)
            {
                await BuildFromEntry(entry, this).ConfigureAwait(false);
            }
        }

        // Add any root nodes that were not present in the layout (layout-first fallback)
        foreach (var entity in this.AttachedObject.RootNodes)
        {
            if (seenNodeIds.Contains(entity.Id))
            {
                continue;
            }

            var rootLayoutNode = new LayoutNodeAdapter(entity);

            // Populate layout children from the scene graph for explorer fallback.
            foreach (var child in entity.Children)
            {
                var childLayout = new LayoutNodeAdapter(child);
                rootLayoutNode.AddLayoutChild(childLayout);
            }

            this.AddChildInternal(rootLayoutNode);
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private async Task LoadSceneChildrenAsync()
    {
        // Original path: build tree using SceneNodeAdapter only (no layout separation)
        var layout = this.AttachedObject.ExplorerLayout;
        var seenNodeIds = new HashSet<System.Guid>();

        if (layout is not null && layout.Count > 0)
        {
            async Task BuildFromEntry(ExplorerEntryData entry, TreeItemAdapter parent)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    var folder = new FolderAdapter(entry);

                    if (entry.Children is not null)
                    {
                        foreach (var childEntry in entry.Children)
                        {
                            await BuildFromEntry(childEntry, folder).ConfigureAwait(false);
                        }
                    }

                    if (parent is FolderAdapter folderParent)
                    {
                        folderParent.AddChildAdapter(folder);
                    }
                    else
                    {
                        this.AddChildInternal(folder);
                    }
                }
                else if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is not null)
                {
                    var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
                    if (node is not null)
                    {
                        var layoutNode = new LayoutNodeAdapter(node);

                        if (entry.IsExpanded.HasValue)
                        {
                            layoutNode.IsExpanded = entry.IsExpanded.Value;
                        }
                        _ = seenNodeIds.Add(node.Id);

                        if (parent is FolderAdapter folderParent)
                        {
                            folderParent.AddChildAdapter(layoutNode);
                        }
                        else if (parent is SceneAdapter sceneParent)
                        {
                            // Only SceneAdapter can attach node adapters at the root level
                            sceneParent.AddChildInternal(layoutNode);
                        }
                    }
                }
            }

            foreach (var entry in layout)
            {
                await BuildFromEntry(entry, this).ConfigureAwait(false);
            }
        }

        // Add any root nodes that were not present in the layout
        foreach (var entity in this.AttachedObject.RootNodes)
        {
            if (!seenNodeIds.Contains(entity.Id))
            {
                var layoutNode = new LayoutNodeAdapter(entity);

                // Populate layout children from scene graph for fallback
                foreach (var child in entity.Children)
                {
                    var childLayout = new LayoutNodeAdapter(child);
                    layoutNode.AddLayoutChild(childLayout);
                }

                this.AddChildInternal(layoutNode);
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    /// <summary>
    ///     Reloads the children collection from the current <see cref="AttachedObject.ExplorerLayout"/>
    ///     or the scene root nodes. This forces a refresh of adapters in-place without creating a
    ///     new SceneAdapter instance which would unnecessarily recreate parent links and view state.
    /// </summary>
    /// <param name="expandedFolderIds">Optional set of folder IDs that should be expanded.</param>
    /// <param name="preserveNodeExpansion">If true, ignores layout expansion state for nodes and uses the current SceneNode state.</param>
    public async Task ReloadChildrenAsync(HashSet<Guid>? expandedFolderIds = null, bool preserveNodeExpansion = false)
    {
        // Clear the cached children then call the same loading path used by InitializeChildrenCollectionAsync
        this.ClearChildren();

        if (this.UseLayoutAdapters)
        {
            await this.LoadLayoutChildrenAsync(expandedFolderIds, preserveNodeExpansion).ConfigureAwait(false);
            return;
        }

        await this.LoadSceneChildrenAsync().ConfigureAwait(false);
    }

    /// <summary>
    /// Retrieves the IDs of all currently expanded folders in the UI tree.
    /// This is used to preserve expansion state during Undo/Redo operations.
    /// </summary>
    public HashSet<Guid> GetExpandedFolderIds()
    {
        var expanded = new HashSet<Guid>();

        // We assume children are loaded when this is called during Undo/Redo.
        // If the Children task is not completed, we block, which is acceptable here
        // as we are restoring state and need the current UI state.
        var rootChildren = this.Children.IsCompleted
            ? this.Children.Result
            : this.Children.GetAwaiter().GetResult();

        if (rootChildren is null)
        {
            return expanded;
        }

        var stack = new Stack<ITreeItem>(rootChildren);

        while (stack.Count > 0)
        {
            var item = stack.Pop();
            if (item is FolderAdapter folder)
            {
                if (folder.IsExpanded)
                {
                    expanded.Add(folder.Id);
                }

                foreach (var child in folder.InternalChildren)
                {
                    stack.Push(child);
                }
            }
        }

        return expanded;
    }
}
