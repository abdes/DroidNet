// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
public partial class SceneAdapter : TreeItemAdapter, ITreeItem<Scene>
{
    // Cache root items to avoid blocking .Result calls on the base Children task
    private readonly List<ITreeItem> rootItemsCache = [];

    /// <summary>Initializes a new instance of the <see cref="SceneAdapter"/> class.</summary>
    /// <param name="scene">The scene to present.</param>
    /// <param name="layout">The scene's explorer layout, which places its rows.</param>
    public SceneAdapter(Scene scene, IList<ExplorerEntryData> layout)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);
        this.AttachedObject = scene;
        this.Layout = layout;
        scene.PropertyChanged += this.OnScenePropertyChanged;
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.AttachedObject.Name;
        set => throw new NotSupportedException("Scene name is read-only presentation; rename scene documents through their owning command surface.");
    }

    /// <inheritdoc />
    public Scene AttachedObject { get; }

    /// <summary>Gets the scene's explorer layout: the folders and node entries the rows follow.</summary>
    public IList<ExplorerEntryData> Layout { get; }

    /// <summary>Gets a value indicating whether the tree uses the authored explorer layout.</summary>
    internal bool UseLayoutAdapters { get; init; }

    /// <summary>Gets the root-level items in layout order without triggering lazy child loading.</summary>
    internal IReadOnlyList<ITreeItem> RootItems => this.rootItemsCache;

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => VirtualPath.ValidateSegment(name, VirtualPathRuleSet.Authored) is null;

    /// <summary>Releases the scene-name subscription when the tree is replaced or disposed.</summary>
    public void Detach() => this.AttachedObject.PropertyChanged -= this.OnScenePropertyChanged;

    /// <summary>Rebuilds the tree while restoring the supplied expansion state.</summary>
    /// <param name="expandedFolderIds">The folders to expand after rebuilding.</param>
    /// <param name="preserveNodeExpansion">Whether to retain node expansion from the model.</param>
    /// <param name="transientlyExpandedFolderIds">
    ///     Folders whose expansion is transient consumer state (for example a search reveal): the
    ///     rebuilt adapter is marked transient before the expansion is applied, so restoring the
    ///     expanded view never writes <c>entry.IsExpanded</c> into the authored layout.
    /// </param>
    /// <returns>The asynchronous rebuild task.</returns>
    public async Task ReloadChildrenAsync(
        ISet<FolderId>? expandedFolderIds = null,
        bool preserveNodeExpansion = false,
        ISet<FolderId>? transientlyExpandedFolderIds = null)
    {
        this.ClearChildren();
        this.rootItemsCache.Clear();
        await this.RebuildTreeAsync(expandedFolderIds, preserveNodeExpansion, transientlyExpandedFolderIds).ConfigureAwait(true);
    }

    /// <summary>
    /// Retrieves the IDs of all currently expanded folders in the UI tree.
    /// Uses the internal cache to avoid deadlocks on the UI thread.
    /// </summary>
    /// <param name="transientOnly">Whether to include only transiently expanded folders.</param>
    /// <returns>The identities of expanded folders.</returns>
    public ISet<FolderId> GetExpandedFolderIds(bool transientOnly = false)
    {
        var expanded = new HashSet<FolderId>();

        // Use the cache! No more .Result deadlocks.
        var stack = new Stack<ITreeItem>(this.rootItemsCache);

        while (stack.Count > 0)
        {
            var item = stack.Pop();
            if (item is FolderAdapter folder)
            {
                if (folder.IsExpanded && (!transientOnly || folder.IsExpansionTransient))
                {
                    expanded.Add(folder.Id);
                }

                // LayoutItemAdapter exposes CurrentChildren synchronously
                foreach (var child in folder.CurrentChildren)
                {
                    stack.Push(child);
                }
            }
            else if (item is SceneNodeAdapter node)
            {
                // Nodes can also contain folders now
                foreach (var child in node.CurrentChildren)
                {
                    stack.Push(child);
                }
            }
        }

        return expanded;
    }

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => this.AttachedObject.RootNodes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        this.rootItemsCache.Clear();

        await this.RebuildTreeAsync().ConfigureAwait(true);
    }

    private async Task RebuildTreeAsync(
        ISet<FolderId>? expandedFolderIds = null,
        bool preserveNodeExpansion = false,
        ISet<FolderId>? transientlyExpandedFolderIds = null)
    {
        var layout = this.Layout;
        var seenNodeIds = new HashSet<NodeId>();
        var seenFolderIds = new HashSet<FolderId>();

        // 1. Build from Layout (The "Seating Chart")
        if (this.UseLayoutAdapters && layout is { Count: > 0 })
        {
            // Index the scene graph once per rebuild so each entry resolves in O(1); the first node wins on duplicate ids.
            var nodesById = new Dictionary<NodeId, SceneNode>();
            foreach (var node in this.AttachedObject.AllNodes)
            {
                _ = nodesById.TryAdd(node.Id, node);
            }

            var realization = new LayoutRealization(nodesById, seenNodeIds, seenFolderIds, expandedFolderIds, preserveNodeExpansion, transientlyExpandedFolderIds);
            foreach (var entry in layout)
            {
                await this.ProcessLayoutEntryAsync(entry, this, realization).ConfigureAwait(true);
            }
        }

        // Layout is an overlay, not a filter: newly created descendants without layout seats
        // still belong in the hierarchy. Already seated nodes keep their authored placement.
        var realizedNodes = new Dictionary<NodeId, SceneNodeAdapter>();
        var pending = new Stack<ITreeItem>(this.rootItemsCache);
        while (pending.TryPop(out var item))
        {
            if (item is SceneNodeAdapter nodeAdapter)
            {
                realizedNodes.Add(nodeAdapter.AttachedObject.Id, nodeAdapter);
            }

            if (item is LayoutItemAdapter layoutItem)
            {
                foreach (var child in layoutItem.CurrentChildren)
                {
                    pending.Push(child);
                }
            }
        }

        foreach (var node in this.AttachedObject.RootNodes)
        {
            this.PopulateUnseatedNodes(node, this, realizedNodes);
        }
    }

    private void PopulateUnseatedNodes(SceneNode node, ITreeItem parent, Dictionary<NodeId, SceneNodeAdapter> realizedNodes)
    {
        if (!realizedNodes.TryGetValue(node.Id, out var adapter))
        {
            adapter = new SceneNodeAdapter(node);
            realizedNodes.Add(node.Id, adapter);
            this.AddToParent(parent, adapter);
        }

        foreach (var child in node.Children)
        {
            this.PopulateUnseatedNodes(child, adapter, realizedNodes);
        }
    }

    private async Task ProcessLayoutEntryAsync(ExplorerEntryData entry, ITreeItem parent, LayoutRealization realization)
    {
        if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
        {
            await this.ProcessFolderEntryAsync(entry, parent, realization).ConfigureAwait(true);
        }
        else if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId.HasValue)
        {
            await this.ProcessNodeEntryAsync(entry, entry.NodeId.Value, parent, realization).ConfigureAwait(true);
        }
    }

    private async Task ProcessFolderEntryAsync(ExplorerEntryData entry, ITreeItem parent, LayoutRealization realization)
    {
        // Claim the id before realizing so a duplicate layout entry cannot realize a second
        // adapter sharing one folder identity; a skipped entry must not recurse into its
        // children either. Entries without a folder id are not stably addressable and realize
        // with a fresh identity per rebuild, so they cannot collide.
        if (entry.FolderId is { } folderId && !realization.SeenFolderIds.Add(folderId))
        {
            return;
        }

        var folder = new FolderAdapter(entry);

        // A transient (search-driven) expansion must be marked before it is applied, or the
        // expanded view would be re-authored into the layout entry on this rebuild.
        if (entry.FolderId is { } transientId && realization.TransientlyExpandedFolderIds?.Contains(transientId) == true)
        {
            folder.SetExpansionTransient(transient: true);
        }

        // Restore expansion state
        if ((entry.FolderId is { } expandedId && realization.ExpandedFolderIds?.Contains(expandedId) == true) || entry.IsExpanded == true)
        {
            folder.IsExpanded = true;
        }

        foreach (var childEntry in entry.Children ?? [])
        {
            await this.ProcessLayoutEntryAsync(childEntry, folder, realization).ConfigureAwait(true);
        }

        this.AddToParent(parent, folder);
    }

    private async Task ProcessNodeEntryAsync(ExplorerEntryData entry, NodeId nodeId, ITreeItem parent, LayoutRealization realization)
    {
        // Claim the id before realizing so a duplicate layout entry cannot realize a second
        // adapter for the same node; a skipped entry must not recurse into its children either.
        if (!realization.NodesById.TryGetValue(nodeId, out var node) || !realization.SeenNodeIds.Add(node.Id))
        {
            return;
        }

        var adapter = new SceneNodeAdapter(node);
        if (!realization.PreserveNodeExpansion && entry.IsExpanded.HasValue)
        {
            adapter.IsExpanded = entry.IsExpanded.Value;
        }

        foreach (var childEntry in entry.Children ?? [])
        {
            await this.ProcessLayoutEntryAsync(childEntry, adapter, realization).ConfigureAwait(true);
        }

        this.AddToParent(parent, adapter);
    }

    private void AddToParent(ITreeItem parent, LayoutItemAdapter child)
    {
        switch (parent)
        {
            case LayoutItemAdapter layoutParent:
                if (child is FolderAdapter f)
                {
                    layoutParent.AddFolder(f);
                }
                else
                {
                    layoutParent.AddContent(child);
                }

                break;
            case SceneAdapter:
                this.AddChildSafe(child);
                break;
        }
    }

    private void AddChildSafe(TreeItemAdapter item)
    {
        this.AddChildInternal(item);
        this.rootItemsCache.Add(item);
    }

    private void OnScenePropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(Scene.Name), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.Label));
        }
    }

    /// <summary>The state one rebuild of the layout shares across its entries.</summary>
    /// <param name="NodesById">The scene's nodes by id; the first node wins on duplicate ids.</param>
    /// <param name="SeenNodeIds">The nodes already given an adapter.</param>
    /// <param name="SeenFolderIds">The folders already given an adapter.</param>
    /// <param name="ExpandedFolderIds">The folders to show expanded, if any.</param>
    /// <param name="PreserveNodeExpansion">Whether node adapters keep their own expansion instead of the layout's.</param>
    /// <param name="TransientlyExpandedFolderIds">The folders a search expanded, which the layout does not record.</param>
    private sealed record LayoutRealization(
        Dictionary<NodeId, SceneNode> NodesById,
        HashSet<NodeId> SeenNodeIds,
        HashSet<FolderId> SeenFolderIds,
        ISet<FolderId>? ExpandedFolderIds,
        bool PreserveNodeExpansion,
        ISet<FolderId>? TransientlyExpandedFolderIds);
}
