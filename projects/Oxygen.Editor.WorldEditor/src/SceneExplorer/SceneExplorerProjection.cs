// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Owns the Scene Explorer's stable node/folder adapter index and adapter tracking for the loaded
/// scene, plus the authored-layout domain view used by search. Lookups resolve by identity without
/// scanning the tree or force-loading collapsed subtrees, and removed/reloaded adapters have their
/// model observers detached.
/// </summary>
public sealed class SceneExplorerProjection
{
    private readonly Dictionary<Guid, SceneNodeAdapter> nodeAdapters = [];
    private readonly Dictionary<Guid, FolderAdapter> folderAdapters = [];
    private readonly HashSet<ITreeItem> trackedItems = [];
    private readonly List<LayoutFolder> layoutFolders = [];
    private readonly Dictionary<Guid, LayoutAncestor[]> nodeLayoutChains = [];
    private readonly Dictionary<ITreeItem, ITreeItem> parentAdapters = new(ReferenceEqualityComparer.Instance);

    /// <summary>Gets the indexed node adapters.</summary>
    public IReadOnlyCollection<SceneNodeAdapter> Nodes => this.nodeAdapters.Values;

    /// <summary>Gets the indexed folder adapters.</summary>
    public IReadOnlyCollection<FolderAdapter> Folders => this.folderAdapters.Values;

    /// <summary>Gets the folders declared by the authored explorer layout, independent of adapter realization.</summary>
    public IReadOnlyList<LayoutFolder> LayoutFolders => this.layoutFolders;

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

    /// <summary>
    /// Gets the top-down authored-layout ancestor chain for the given node identity. The chain covers
    /// visual folders and layout node seats independent of realization and expansion state.
    /// </summary>
    /// <param name="nodeId">The node identity.</param>
    /// <returns>The ancestors from the layout root down to the node's parent, or <see langword="null"/> when the node has no layout seat.</returns>
    public IReadOnlyList<LayoutAncestor>? GetNodeLayoutAncestors(Guid nodeId)
        => this.nodeLayoutChains.TryGetValue(nodeId, out var chain) ? chain : null;

    /// <summary>Gets the realized projection ancestors without loading collapsed child collections.</summary>
    /// <param name="item">The adapter whose ancestors are requested.</param>
    /// <returns>The ancestor adapters in root-to-parent order.</returns>
    public IReadOnlyList<ITreeItem> GetAncestors(ITreeItem item)
    {
        var ancestors = new Stack<ITreeItem>();
        while (this.parentAdapters.TryGetValue(item, out var parent))
        {
            ancestors.Push(parent);
            item = parent;
        }

        return ancestors.ToArray();
    }

    /// <summary>Rebuilds the node and folder indexes from the realized adapter tree without force-loading collapsed subtrees, and reindexes the authored layout domain.</summary>
    /// <param name="sceneAdapter">The scene adapter rooting the tree.</param>
    public void Rebuild(SceneAdapter sceneAdapter)
    {
        this.Clear();
        this.Track(sceneAdapter);

        var stack = new Stack<(ITreeItem Item, ITreeItem Parent)>(
            sceneAdapter.RootItems.Select(item => (item, (ITreeItem)sceneAdapter)));
        while (stack.Count > 0)
        {
            var (item, parent) = stack.Pop();
            this.parentAdapters[item] = parent;
            this.Track(item);
            this.Index(item);

            if (item is LayoutItemAdapter layoutItem)
            {
                foreach (var child in layoutItem.CurrentChildren)
                {
                    stack.Push((child, item));
                }
            }
        }

        this.RebuildLayoutDomain(sceneAdapter.AttachedObject.ExplorerLayout);
    }

    /// <summary>Clears both indexes, the authored layout domain and detaches all tracked adapters.</summary>
    public void Clear()
    {
        this.nodeAdapters.Clear();
        this.folderAdapters.Clear();
        this.layoutFolders.Clear();
        this.nodeLayoutChains.Clear();
        this.parentAdapters.Clear();
        this.ClearTracked();
    }

    /// <summary>Indexes a realized adapter by identity.</summary>
    /// <param name="item">The adapter to index.</param>
    public void Index(ITreeItem item)
    {
        if (item.Parent is { } parent)
        {
            this.parentAdapters[item] = parent;
        }

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
        _ = this.parentAdapters.Remove(item);
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

    private void RebuildLayoutDomain(IList<ExplorerEntryData>? layout)
    {
        if (layout is null)
        {
            return;
        }

        // Domain view over the authored layout: folder entries and node ancestor chains, indexed
        // independently of adapter realization. The claim rules mirror layout realization — the first
        // entry for a folder or node identity wins and a skipped duplicate contributes no children.
        // Folder entries without an id are not stably addressable; they resolve to no adapter.
        var seenFolderIds = new HashSet<Guid>();
        var seenNodeIds = new HashSet<Guid>();
        var chain = new List<LayoutAncestor>();

        Walk(layout);

        void Walk(IList<ExplorerEntryData> entries)
        {
            foreach (var entry in entries)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    if (entry.FolderId is { } folderId && !seenFolderIds.Add(folderId))
                    {
                        continue;
                    }

                    this.layoutFolders.Add(new LayoutFolder(entry.FolderId, entry.Name ?? FolderAdapter.FallbackName));
                    chain.Add(new LayoutAncestor(IsFolder: true, entry.FolderId));
                    if (entry.Children is { } folderChildren)
                    {
                        Walk(folderChildren);
                    }

                    chain.RemoveAt(chain.Count - 1);
                }
                else if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is { } nodeId)
                {
                    if (!seenNodeIds.Add(nodeId))
                    {
                        continue;
                    }

                    this.nodeLayoutChains[nodeId] = [.. chain];
                    chain.Add(new LayoutAncestor(IsFolder: false, nodeId));
                    if (entry.Children is { } nodeChildren)
                    {
                        Walk(nodeChildren);
                    }

                    chain.RemoveAt(chain.Count - 1);
                }
            }
        }
    }

    /// <summary>A folder declared by the authored explorer layout, whether or not it is realized as an adapter.</summary>
    /// <param name="Id">The stable folder identity, or <see langword="null"/> when the entry is not stably addressable.</param>
    /// <param name="Name">The folder display name, using the same fallback as the realized adapter label.</param>
    public readonly record struct LayoutFolder(Guid? Id, string Name);

    /// <summary>An ancestor link in an authored-layout entry's chain.</summary>
    /// <param name="IsFolder">Whether the ancestor is a visual folder rather than a scene node seat.</param>
    /// <param name="Id">The ancestor identity, or <see langword="null"/> for a folder entry without a stable id.</param>
    public readonly record struct LayoutAncestor(bool IsFolder, Guid? Id);
}
