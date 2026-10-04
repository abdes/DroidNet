// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Owns the Scene Explorer's stable node/folder adapter index and adapter tracking for the loaded
/// scene. Lookups resolve by identity without scanning the tree or force-loading collapsed subtrees,
/// and removed/reloaded adapters have their model observers detached.
/// </summary>
public sealed class SceneExplorerProjection
{
    private readonly Dictionary<Guid, SceneNodeAdapter> nodeAdapters = [];
    private readonly Dictionary<Guid, FolderAdapter> folderAdapters = [];
    private readonly HashSet<ITreeItem> trackedItems = [];

    /// <summary>Gets the indexed node adapters.</summary>
    public IReadOnlyCollection<SceneNodeAdapter> Nodes => this.nodeAdapters.Values;

    /// <summary>Gets the indexed folder adapters.</summary>
    public IReadOnlyCollection<FolderAdapter> Folders => this.folderAdapters.Values;

    /// <summary>Gets the node adapter for the given identity, or <see langword="null"/> when not realized.</summary>
    /// <param name="nodeId">The node identity.</param>
    /// <returns>The adapter, or <see langword="null"/>.</returns>
    public SceneNodeAdapter? GetNode(Guid nodeId)
        => this.nodeAdapters.TryGetValue(nodeId, out var adapter) ? adapter : null;

    /// <summary>Gets the folder adapter for the given identity, or <see langword="null"/> when not realized.</summary>
    /// <param name="folderId">The folder identity.</param>
    /// <returns>The adapter, or <see langword="null"/>.</returns>
    public FolderAdapter? GetFolder(Guid folderId)
        => this.folderAdapters.TryGetValue(folderId, out var adapter) ? adapter : null;

    /// <summary>Gets a value indicating whether a node adapter is indexed for the given identity.</summary>
    /// <param name="nodeId">The node identity.</param>
    /// <returns><see langword="true"/> when indexed; otherwise <see langword="false"/>.</returns>
    public bool ContainsNode(Guid nodeId) => this.nodeAdapters.ContainsKey(nodeId);

    /// <summary>Gets a value indicating whether a folder adapter is indexed for the given identity.</summary>
    /// <param name="folderId">The folder identity.</param>
    /// <returns><see langword="true"/> when indexed; otherwise <see langword="false"/>.</returns>
    public bool ContainsFolder(Guid folderId) => this.folderAdapters.ContainsKey(folderId);

    /// <summary>Rebuilds the node and folder indexes from the realized adapter tree without force-loading collapsed subtrees.</summary>
    /// <param name="sceneAdapter">The scene adapter rooting the tree.</param>
    public void Rebuild(SceneAdapter sceneAdapter)
    {
        this.Clear();
        this.Track(sceneAdapter);

        var stack = new Stack<ITreeItem>(sceneAdapter.RootItems);
        while (stack.Count > 0)
        {
            var item = stack.Pop();
            this.Track(item);
            this.Index(item);

            if (item is LayoutItemAdapter layoutItem)
            {
                foreach (var child in layoutItem.CurrentChildren)
                {
                    stack.Push(child);
                }
            }
        }
    }

    /// <summary>Clears both indexes and detaches all tracked adapters.</summary>
    public void Clear()
    {
        this.nodeAdapters.Clear();
        this.folderAdapters.Clear();
        this.ClearTracked();
    }

    /// <summary>Indexes a realized adapter by identity.</summary>
    /// <param name="item">The adapter to index.</param>
    public void Index(ITreeItem item)
    {
        switch (item)
        {
            case SceneNodeAdapter node:
                this.nodeAdapters[node.AttachedObject.Id] = node;
                break;
            case FolderAdapter folder:
                this.folderAdapters[folder.Id] = folder;
                break;
        }
    }

    /// <summary>Removes a realized adapter from the index.</summary>
    /// <param name="item">The adapter to unindex.</param>
    public void Unindex(ITreeItem item)
    {
        switch (item)
        {
            case SceneNodeAdapter node:
                _ = this.nodeAdapters.Remove(node.AttachedObject.Id);
                break;
            case FolderAdapter folder:
                _ = this.folderAdapters.Remove(folder.Id);
                break;
        }
    }

    /// <summary>Tracks an adapter so its model observer is detached on removal/reload.</summary>
    /// <param name="item">The adapter to track.</param>
    public void Track(ITreeItem item) => _ = this.trackedItems.Add(item);

    /// <summary>Untracks an adapter (and its loaded children), detaching their model observers.</summary>
    /// <param name="item">The adapter to untrack.</param>
    public void Untrack(ITreeItem item)
    {
        if (item is SceneNodeAdapter nodeAdapter)
        {
            nodeAdapter.Detach();
        }

        if (item is TreeItemAdapter adapter && adapter.TryGetLoadedChildren(out var children))
        {
            foreach (var child in children)
            {
                this.Untrack(child);
            }
        }

        _ = this.trackedItems.Remove(item);
    }

    private void ClearTracked()
    {
        foreach (var item in this.trackedItems)
        {
            if (item is SceneNodeAdapter nodeAdapter)
            {
                nodeAdapter.Detach();
            }
        }

        this.trackedItems.Clear();
    }
}
