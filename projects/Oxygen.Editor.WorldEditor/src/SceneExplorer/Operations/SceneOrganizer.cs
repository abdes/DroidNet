// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Default implementation of <see cref="ISceneOrganizer"/> that manages a scene's
/// <see cref="Scene.ExplorerLayout"/> representation and reconciles the visual
/// tree adapters used by the scene explorer. Methods in this class operate on
/// the layout model and do not directly mutate the engine's runtime scene
/// graph; higher-level services are responsible for performing engine-sync
/// mutations when required.
/// </summary>
/// <param name="logger">The logger used for diagnostic messages.</param>
public sealed partial class SceneOrganizer(ILogger<SceneOrganizer> logger) : ISceneOrganizer
{
    private static readonly StringComparer TypeComparer = StringComparer.OrdinalIgnoreCase;

    private readonly ILogger<SceneOrganizer> logger = logger ?? throw new ArgumentNullException(nameof(logger));

    /// <summary>
    /// Moves the layout entry representing <paramref name="nodeId"/> into the
    /// folder identified by <paramref name="folderId"/>. The method operates
    /// on the provided <paramref name="scene"/>'s <see cref="Scene.ExplorerLayout"/>.
    /// </summary>
    /// <param name="nodeId">Id of the node to move.</param>
    /// <param name="folderId">Id of the target folder.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the folder cannot be found
    /// or the move would violate folder-lineage constraints.</exception>
    public LayoutChangeRecord MoveNodeToFolder(Guid nodeId, Guid folderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.LogSceneInfo("MoveNodeToFolder (Start)", scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, folderParentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null)
        {
            var allFolders = new List<Guid>();
            void CollectFolders(IList<ExplorerEntryData> entries)
            {
                foreach (var e in entries)
                {
                    if (e.FolderId.HasValue)
                    {
                        allFolders.Add(e.FolderId.Value);
                    }

                    if (e.Children != null)
                    {
                        CollectFolders(e.Children);
                    }
                }
            }

            CollectFolders(layout);
            this.LogFolderNotFound(folderId, allFolders);
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        if (!IsNodeUnderFolderLineage(layout, nodeId, folderEntry, scene))
        {
            throw new InvalidOperationException("Cannot move node to a folder outside its lineage.");
        }

        // Remove node from any existing location
        var removedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, [nodeId], removedEntries);
        this.LogMoveNodeRemoved(removedEntries.Count, nodeId);

        var nodeEntry = removedEntries.FirstOrDefault() ?? new ExplorerEntryData { Type = "Node", NodeId = nodeId };

        folderEntry.Children!.Add(nodeEntry);
        this.LogMoveNodeToFolder(nodeId, folderEntry.FolderId!.Value);

        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "MoveNodeToFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry],
            ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
    }

    /// <summary>
    /// Removes the layout entry for <paramref name="nodeId"/> from the folder
    /// identified by <paramref name="folderId"/> (if present).
    /// </summary>
    /// <param name="nodeId">Id of the node to remove from the folder.</param>
    /// <param name="folderId">Id of the folder to remove from.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the folder cannot be found.</exception>
    public LayoutChangeRecord RemoveNodeFromFolder(Guid nodeId, Guid folderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, folderParentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        if (folderEntry.Children is null)
        {
            // Nothing to remove
            return new LayoutChangeRecord(
                OperationName: "RemoveNodeFromFolder",
                PreviousLayout: previousLayout,
                NewLayout: layout,
                ModifiedFolders: [folderEntry],
                ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
        }

        var nodeEntry = folderEntry.Children.FirstOrDefault(e => TypeComparer.Equals(e.Type, "Node") && e.NodeId == nodeId);
        if (nodeEntry is not null)
        {
            _ = folderEntry.Children.Remove(nodeEntry);
            this.LogRemoveNodeFromFolder(nodeId, folderId);
        }

        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "RemoveNodeFromFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry],
            ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
    }

    /// <summary>
    /// Removes all layout entries that reference <paramref name="nodeId"/> from
    /// the scene layout. This is a model-only operation and does not change the
    /// scene graph itself.
    /// </summary>
    /// <param name="nodeId">Id of the node to remove from the layout.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    public LayoutChangeRecord RemoveNodeFromLayout(Guid nodeId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var removedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, [nodeId], removedEntries);
        this.LogRemoveNodeFromLayout(removedEntries.Count, nodeId);

        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "RemoveNodeFromLayout",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            NewFolder: null,
            ModifiedFolders: []);
    }

    private void EnsureNodeInLayout(IList<ExplorerEntryData> layout, Guid nodeId, Scene scene)
    {
        // Check if already in layout
        var (existing, _) = this.FindNodeEntryWithParent(layout, nodeId);
        if (existing != null)
        {
            return;
        }

        // Find in Scene Graph
        var node = FindNodeById(scene, nodeId) ?? throw new InvalidOperationException($"Node {nodeId} not found in scene.");

        // Ensure parent is in layout
        IList<ExplorerEntryData> targetList;
        if (node.Parent == null)
        {
            // Root node
            targetList = layout;
        }
        else
        {
            this.EnsureNodeInLayout(layout, node.Parent.Id, scene);
            var (parentEntry, _) = this.FindNodeEntryWithParent(layout, node.Parent.Id);
            if (parentEntry == null)
            {
                // Should not happen after EnsureNodeInLayout
                throw new InvalidOperationException($"Failed to ensure parent node {node.Parent.Id} in layout.");
            }

            parentEntry.Children ??= [];
            targetList = parentEntry.Children;
        }

        // Add to layout
        targetList.Add(new ExplorerEntryData { Type = "Node", NodeId = nodeId });
    }

    /// <summary>
    /// Creates a new folder entry in <paramref name="scene"/>'s layout.
    /// The folder may be created at the root, under an existing folder
    /// (<paramref name="parentFolderId"/>), or as a child of a node in the
    /// layout (<paramref name="parentNodeId"/>).
    /// </summary>
    /// <param name="parentFolderId">Optional id of the parent folder.</param>
    /// <param name="name">The display name of the new folder.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <param name="folderId">Optional id to assign to the new folder. If null a new id will be generated.</param>
    /// <param name="parentNodeId">Optional node id to create the folder under.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change, including the created folder.</returns>
    /// <exception cref="InvalidOperationException">Thrown when a specified parent folder or parent node cannot be found.</exception>
    public LayoutChangeRecord CreateFolder(Guid? parentFolderId, string name, Scene scene, Guid? folderId = null, Guid? parentNodeId = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.LogSceneInfo("CreateFolder (Start)", scene);
        this.LogCreateFolder(name, parentFolderId, parentNodeId, folderId);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var newFolderId = folderId ?? Guid.NewGuid();
        var folderEntry = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = newFolderId,
            Name = name,
            Children = [],
        };

        if (parentFolderId.HasValue)
        {
            var (parentEntry, _) = FindFolderEntryWithParent(layout, parentFolderId.Value);
            if (parentEntry == null)
            {
                throw new InvalidOperationException($"Parent folder {parentFolderId} not found.");
            }

            parentEntry.Children ??= [];
            parentEntry.Children.Add(folderEntry);
        }
        else if (parentNodeId.HasValue)
        {
            this.EnsureNodeInLayout(layout, parentNodeId.Value, scene);
            var (parentEntry, _) = this.FindNodeEntryWithParent(layout, parentNodeId.Value);
            if (parentEntry == null)
            {
                throw new InvalidOperationException($"Parent node {parentNodeId} not found in layout.");
            }

            parentEntry.Children ??= [];
            parentEntry.Children.Add(folderEntry);
        }
        else
        {
            layout.Add(folderEntry);
        }

        scene.ExplorerLayout = layout;
        this.LogSceneInfo("CreateFolder (End)", scene);

        return new LayoutChangeRecord(
            OperationName: "CreateFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            NewFolder: folderEntry,
            ModifiedFolders: [folderEntry]);
    }

    private (ExplorerEntryData? Entry, IList<ExplorerEntryData>? ParentList) FindNodeEntryWithParent(IList<ExplorerEntryData> entries, Guid nodeId)
    {
        foreach (var entry in entries)
        {
            if (TypeComparer.Equals(entry.Type, "Node") && entry.NodeId == nodeId)
            {
                return (entry, entries);
            }

            if (entry.Children != null && entry.Children.Count > 0)
            {
                var (found, parent) = this.FindNodeEntryWithParent(entry.Children, nodeId);
                if (found != null)
                {
                    return (found, parent);
                }
            }
        }

        return (null, null);
    }

    /// <summary>
    /// Moves the folder identified by <paramref name="folderId"/> to be a child
    /// of <paramref name="newParentFolderId"/>. Passing <see langword="null"/>
    /// for <paramref name="newParentFolderId"/> places the folder at the root level.
    /// </summary>
    /// <param name="folderId">Id of the folder being moved.</param>
    /// <param name="newParentFolderId">Id of the new parent folder, or null for root.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the folder or target parent folder cannot be found.</exception>
    public LayoutChangeRecord MoveFolderToParent(Guid folderId, Guid? newParentFolderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, parentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null || parentList is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        _ = parentList.Remove(folderEntry);

        if (newParentFolderId is null)
        {
            layout.Add(folderEntry);
        }
        else
        {
            var (targetParent, _) = FindFolderEntryWithParent(layout, newParentFolderId.Value);
            if (targetParent is null)
            {
                throw new InvalidOperationException($"Target parent folder '{newParentFolderId}' not found.");
            }

            targetParent.Children!.Add(folderEntry);
        }

        scene.ExplorerLayout = layout;
        this.LogMoveFolderToParent(folderId, newParentFolderId);

        return new LayoutChangeRecord(
            OperationName: "MoveFolderToParent",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry]);
    }

    /// <summary>
    /// Removes the folder with id <paramref name="folderId"/> from the layout.
    /// If <paramref name="promoteChildrenToParent"/> is true any children of the
    /// removed folder will be inserted into the folder's parent list.
    /// </summary>
    /// <param name="folderId">Id of the folder to remove.</param>
    /// <param name="promoteChildrenToParent">Whether to move children to the folder's parent list.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the folder cannot be found.</exception>
    public LayoutChangeRecord RemoveFolder(Guid folderId, bool promoteChildrenToParent, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, parentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null || parentList is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        _ = parentList.Remove(folderEntry);

        if (promoteChildrenToParent && folderEntry.Children is not null && folderEntry.Children.Count > 0)
        {
            foreach (var child in folderEntry.Children)
            {
                parentList.Add(child);
            }
        }

        scene.ExplorerLayout = layout;
        this.LogRemoveFolder(folderId, promoteChildrenToParent);

        return new LayoutChangeRecord(
            OperationName: "RemoveFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry]);
    }

    /// <summary>
    /// Renames the folder identified by <paramref name="folderId"/> to
    /// <paramref name="newName"/> within the scene layout.
    /// </summary>
    /// <param name="folderId">Id of the folder to rename.</param>
    /// <param name="newName">The new display name for the folder.</param>
    /// <param name="scene">The scene whose layout will be modified.</param>
    /// <returns>A <see cref="LayoutChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the folder cannot be found.</exception>
    public LayoutChangeRecord RenameFolder(Guid folderId, string newName, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, _) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        folderEntry.Name = newName;
        scene.ExplorerLayout = layout;
        this.LogRenameFolder(folderId, newName);

        return new LayoutChangeRecord(
            OperationName: "RenameFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry]);
    }

    private static SceneNodeAdapter? WrapAsSceneNodeAdapter(TreeItemAdapter adapter)
        => adapter switch
        {
            SceneNodeAdapter existing => existing,
            _ => null,
        };

    private static async Task<List<ITreeItem>> FlattenVisibleAsync(ITreeItem root)
    {
        var list = new List<ITreeItem> { root };

        if (!root.IsExpanded)
        {
            return list;
        }

        var children = await root.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            list.AddRange(await FlattenVisibleAsync(child).ConfigureAwait(true));
        }

        return list;
    }

    private static async Task MoveAdapterAndRefreshAsync(
        TreeItemAdapter nodeAdapter,
        FolderAdapter folderAdapter,
        bool wasExpanded,
        SceneAdapter sceneAdapter,
        ILayoutContext layoutContext)
    {
        if (nodeAdapter.Parent is TreeItemAdapter parentAdapter)
        {
            _ = await parentAdapter.RemoveChildAsync(nodeAdapter).ConfigureAwait(true);
        }

        var sceneNodeAdapter = WrapAsSceneNodeAdapter(nodeAdapter);
        if (sceneNodeAdapter is null)
        {
            return;
        }

        sceneNodeAdapter.IsExpanded = wasExpanded;
        folderAdapter.AddContent(sceneNodeAdapter);
        await layoutContext.RefreshTreeAsync(sceneAdapter).ConfigureAwait(true);
    }

    public async Task ReconcileLayoutAsync(
        SceneAdapter sceneAdapter,
        Scene scene,
        IList<ExplorerEntryData>? layout,
        ILayoutContext layoutContext,
        bool preserveNodeExpansion = false)
    /// <summary>
    /// Reconciles the provided layout (or builds one from the scene root nodes if
    /// <paramref name="layout"/> is null) with the given <paramref name="sceneAdapter"/>.
    /// This method attaches and detaches adapters to reflect the layout and ensures
    /// that all visible nodes are represented in the visual tree.
    /// </summary>
    /// <param name="sceneAdapter">Root adapter for the scene's visual representation.</param>
    /// <param name="scene">The scene whose layout is being reconciled.</param>
    /// <param name="layout">Optional layout to apply; if null the layout will be built from root nodes.</param>
    /// <param name="layoutContext">Context used to refresh the visual tree.</param>
    /// <param name="preserveNodeExpansion">If true, preserves node expansion states where possible.</param>
    /// <returns>A task representing the asynchronous reconcile operation.</returns>
    {
        ArgumentNullException.ThrowIfNull(sceneAdapter);
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layoutContext);

        var targetLayout = layout ?? BuildLayoutFromRootNodes(scene);
        var layoutClone = this.CloneLayout(targetLayout) ?? [];
        this.LogReconcileStart(scene.GetHashCode(), layoutClone.Count);
        scene.ExplorerLayout = layoutClone;

        var adapterIndex = await BuildAdapterIndexAsync(sceneAdapter).ConfigureAwait(true);
        this.LogAdapterIndexStats(adapterIndex.NodeAdapters.Count, adapterIndex.FolderAdapters.Count, adapterIndex.AllAdapters.Count);

        // Detach all existing adapters from their parents to avoid duplicates during reattach.
        foreach (var adapter in adapterIndex.AllAdapters)
        {
            await DetachAdapterAsync(adapter).ConfigureAwait(true);
        }

        var usedNodeIds = new HashSet<Guid>();
        var usedFolderIds = new HashSet<Guid>();

        foreach (var entry in layoutClone)
        {
            await AttachEntryAsync(
                entry,
                sceneAdapter,
                sceneAdapter,
                adapterIndex,
                usedNodeIds,
                usedFolderIds,
                preserveNodeExpansion)
            .ConfigureAwait(true);
        }

        // Ensure any root nodes missing from layout remain visible by attaching them at the end.
        foreach (var root in scene.RootNodes)
        {
            if (usedNodeIds.Contains(root.Id))
            {
                continue;
            }

            SceneNodeAdapter adapter;
            if (adapterIndex.NodeAdapters.TryGetValue(root.Id, out var existing))
            {
                adapter = existing;
            }
            else
            {
                this.LogAttachingMissingRootNode(root.Id);
                adapter = new SceneNodeAdapter(root);
            }

            await AttachToParentAsync(sceneAdapter, adapter).ConfigureAwait(true);
            usedNodeIds.Add(root.Id);
        }

        await layoutContext.RefreshTreeAsync(sceneAdapter).ConfigureAwait(true);
        this.LogReconcileEnd(usedNodeIds.Count, usedFolderIds.Count);
    }

    private static IList<ExplorerEntryData> RequireLayout(Scene scene)
    {
        if (scene.ExplorerLayout is null)
        {
            throw new InvalidOperationException("ExplorerLayout is not initialized.");
        }

        // Normalize to ensure folder entries always have a mutable children list for in-place updates.
        var normalized = NormalizeLayout(scene.ExplorerLayout);
        scene.ExplorerLayout = normalized;
        return normalized;
    }

    private void LogSceneInfo(string operation, Scene scene)
    {
        var folderCount = 0;
        void CountFolders(IList<ExplorerEntryData> entries)
        {
            foreach (var e in entries)
            {
                if (e.FolderId.HasValue)
                {
                    folderCount++;
                }

                if (e.Children != null)
                {
                    CountFolders(e.Children);
                }
            }
        }

        if (scene.ExplorerLayout != null)
        {
            CountFolders(scene.ExplorerLayout);
        }

        this.LogSceneInfoEvent(operation, scene.GetHashCode(), scene.ExplorerLayout?.Count ?? 0, folderCount);
    }

    private static (ExplorerEntryData? entry, IList<ExplorerEntryData>? parent) FindFolderEntryWithParent(IList<ExplorerEntryData>? entries, Guid id, IList<ExplorerEntryData>? parent = null)
    {
        if (entries is null)
        {
            return (null, null);
        }

        foreach (var e in entries)
        {
            if (string.Equals(e.Type, "Folder", StringComparison.OrdinalIgnoreCase) && e.FolderId == id)
            {
                return (e, parent ?? entries);
            }

            var found = FindFolderEntryWithParent(e.Children, id, e.Children);
            if (found.entry is not null)
            {
                return found;
            }
        }

        return (null, null);
    }

    private static void RemoveEntriesForNodeIds(IList<ExplorerEntryData> entries, HashSet<Guid> nodeIds, IList<ExplorerEntryData> removed)
    {
        for (var i = entries.Count - 1; i >= 0; i--)
        {
            var entry = entries[i];
            if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is Guid nodeId && nodeIds.Contains(nodeId))
            {
                removed.Add(entry);
                entries.RemoveAt(i);
                continue;
            }

            if (entry.Children is not null && entry.Children.Count > 0)
            {
                RemoveEntriesForNodeIds(entry.Children, nodeIds, removed);

                // Prune empty folders after removal
                // if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.Children.Count == 0)
                // {
                //    entries.RemoveAt(i);
                // }
            }
        }
    }

    /// <summary>
    /// Creates a deep clone of the provided layout list, recursively copying
    /// entries and their children. Returns <see langword="null"/> if
    /// <paramref name="layout"/> is <see langword="null"/>.
    /// </summary>
    /// <param name="layout">Layout to clone (or null).</param>
    /// <returns>A deep copy of <paramref name="layout"/>, or null.</returns>
    public IList<ExplorerEntryData>? CloneLayout(IList<ExplorerEntryData>? layout)
    {
        if (layout is null)
        {
            return null;
        }

        return layout.Select(CloneEntry).ToList();
    }

    /// <summary>
    /// Returns the set of folder ids marked as expanded within the provided
    /// <paramref name="layout"/>. The returned set will be empty if
    /// <paramref name="layout"/> is null or no folders are expanded.
    /// </summary>
    /// <param name="layout">Layout to inspect for expanded folders.</param>
    /// <returns>A set of expanded folder ids.</returns>
    public HashSet<Guid> GetExpandedFolderIds(IList<ExplorerEntryData>? layout)
    {
        var expandedIds = new HashSet<Guid>();
        if (layout is null)
        {
            return expandedIds;
        }

        var stack = new Stack<IList<ExplorerEntryData>>();
        stack.Push(layout);

        while (stack.Count > 0)
        {
            var list = stack.Pop();
            foreach (var entry in list)
            {
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    if (entry.IsExpanded == true && entry.FolderId.HasValue)
                    {
                        _ = expandedIds.Add(entry.FolderId.Value);
                    }

                    if (entry.Children is not null)
                    {
                        stack.Push(entry.Children);
                    }
                }
            }
        }

        return expandedIds;
    }

    private static ExplorerEntryData? FindFolderEntry(IList<ExplorerEntryData> entries, Guid folderId)
    {
        foreach (var entry in entries)
        {
            if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.FolderId == folderId)
            {
                return entry;
            }

            if (entry.Children != null)
            {
                var found = FindFolderEntry(entry.Children, folderId);
                if (found != null)
                {
                    return found;
                }
            }
        }

        return null;
    }

    /// <summary>
    /// Ensures that the provided <paramref name="scene"/>'s layout contains
    /// entries for each id in <paramref name="nodeIds"/>. Missing node entries
    /// will be appended to the layout if they are absent.
    /// </summary>
    /// <param name="scene">The scene whose layout will be modified (created if absent).</param>
    /// <param name="nodeIds">A sequence of node ids that must be present in the layout.</param>
    public void EnsureLayoutContainsNodes(Scene scene, IEnumerable<Guid> nodeIds)
    {
        // Ensure layout exists
        scene.ExplorerLayout ??= BuildLayoutFromRootNodes(scene);

        foreach (var id in nodeIds)
        {
            if (LayoutContainsNode(scene.ExplorerLayout, id))
            {
                continue;
            }

            scene.ExplorerLayout.Add(new ExplorerEntryData { Type = "Node", NodeId = id });
        }

        static bool LayoutContainsNode(IList<ExplorerEntryData> layout, Guid nodeId)
        {
            foreach (var entry in layout)
            {
                if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId == nodeId)
                {
                    return true;
                }

                if (entry.Children is not null && LayoutContainsNode(entry.Children, nodeId))
                {
                    return true;
                }
            }

            return false;
        }
    }

    /// <summary>
    /// Builds a layout that contains only the folder described by <paramref name="layoutChange"/>
    /// (inserted at the same position it would appear in the new layout) plus any
    /// original node entries necessary to keep undo semantics consistent.
    /// This helper is useful to create a minimal layout for undo/redo operations.
    /// </summary>
    /// <param name="layoutChange">The layout change describing the new folder and layouts.</param>
    /// <returns>A layout focused on the affected folder suitable for folder-only undo operations.</returns>
    public IList<ExplorerEntryData> BuildFolderOnlyLayout(LayoutChangeRecord layoutChange)
    {
        Console.WriteLine($"DEBUG: BuildFolderOnlyLayout. NewFolder is null? {layoutChange.NewFolder is null}");
        if (layoutChange.NewFolder != null)
        {
            Console.WriteLine($"DEBUG: NewFolder ID: {layoutChange.NewFolder.FolderId}");
        }

        // Start from previous layout (so nodes remain outside the folder for first undo)
        var baseLayout = layoutChange.PreviousLayout is null
            ? []
            : new List<ExplorerEntryData>(layoutChange.PreviousLayout);

        var folder = layoutChange.NewFolder ?? new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = Guid.NewGuid(),
            Name = "Folder",
        };
        Console.WriteLine($"DEBUG: Using folder ID: {folder.FolderId}");

        var emptyFolder = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = folder.FolderId,
            Name = folder.Name,
            Children = [],
            IsExpanded = folder.IsExpanded,
        };

        // Insert folder at the same position it appears in the new layout (best-effort), else append
        var insertIndex = 0;
        if (layoutChange.NewLayout is not null)
        {
            var found = layoutChange.NewLayout.Select((entry, index) => (entry, index))
                .FirstOrDefault(tuple => tuple.entry.FolderId == folder.FolderId);
            insertIndex = found.entry is null ? baseLayout.Count : Math.Min(found.index, baseLayout.Count);
        }
        else
        {
            insertIndex = baseLayout.Count;
        }

        baseLayout.Insert(insertIndex, emptyFolder);

        // Ensure original node entries remain present (if missing due to cloning source)
        if (layoutChange.PreviousLayout is not null)
        {
            foreach (var entry in layoutChange.PreviousLayout)
            {
                // avoid duplicating folder we just inserted
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.FolderId == folder.FolderId)
                {
                    continue;
                }

                if (!baseLayout.Any(e => e.NodeId == entry.NodeId))
                {
                    baseLayout.Add(entry);
                }
            }
        }

        return baseLayout;
    }

    private static ExplorerEntryData CloneEntry(ExplorerEntryData entry)
    {
        return new ExplorerEntryData
        {
            Type = entry.Type,
            FolderId = entry.FolderId,
            NodeId = entry.NodeId,
            Name = entry.Name,
            Children = entry.Children is null ? null : entry.Children.Select(CloneEntry).ToList(),
            IsExpanded = entry.IsExpanded,
        };
    }

    private static IList<ExplorerEntryData> BuildLayoutFromRootNodes(Scene scene)
    {
        var layout = new List<ExplorerEntryData>();
        foreach (var node in scene.RootNodes)
        {
            layout.Add(new ExplorerEntryData { Type = "Node", NodeId = node.Id });
        }

        return layout;
    }

    private static async Task AttachEntryAsync(
        ExplorerEntryData entry,
        TreeItemAdapter parent,
        SceneAdapter sceneAdapter,
        AdapterIndex adapterIndex,
        HashSet<Guid> usedNodeIds,
        HashSet<Guid> usedFolderIds,
        bool preserveNodeExpansion)
    {
        if (TypeComparer.Equals(entry.Type, "Folder") && entry.FolderId.HasValue)
        {
            var folder = adapterIndex.FolderAdapters.TryGetValue(entry.FolderId.Value, out var existingFolder)
                ? existingFolder
                : new FolderAdapter(entry.FolderId.Value, entry.Name ?? "Folder");

            folder.IsExpanded = entry.IsExpanded ?? folder.IsExpanded;
            usedFolderIds.Add(folder.Id);

            await AttachToParentAsync(parent, folder).ConfigureAwait(true);

            if (entry.Children is not null)
            {
                foreach (var child in entry.Children)
                {
                    await AttachEntryAsync(child, folder, sceneAdapter, adapterIndex, usedNodeIds, usedFolderIds, preserveNodeExpansion)
                        .ConfigureAwait(true);
                }
            }

            return;
        }

        if (!TypeComparer.Equals(entry.Type, "Node") || entry.NodeId is null)
        {
            return;
        }

        var nodeId = entry.NodeId.Value;
        if (!adapterIndex.NodesById.TryGetValue(nodeId, out var payloadNode))
        {
            return;
        }

        var adapter = adapterIndex.NodeAdapters.TryGetValue(nodeId, out var existingAdapter)
            ? existingAdapter
            : new SceneNodeAdapter(payloadNode);

        if (!preserveNodeExpansion && entry.IsExpanded.HasValue)
        {
            adapter.IsExpanded = entry.IsExpanded.Value;
        }

        usedNodeIds.Add(nodeId);

        // Attach to current parent context: decide by inspecting entry placement
        await AttachToParentAsync(parent, adapter).ConfigureAwait(true);

        if (entry.Children is not null && entry.Children.Count > 0)
        {
            foreach (var child in entry.Children)
            {
                await AttachEntryAsync(child, adapter, sceneAdapter, adapterIndex, usedNodeIds, usedFolderIds, preserveNodeExpansion)
                    .ConfigureAwait(true);
            }
        }
        else if (adapter.ChildrenCount == 0 && adapter.AttachedObject.Children.Count > 0)
        {
            // Layout fallback: mirror scene graph children so nodes remain visible even without layout entries.
            foreach (var child in adapter.AttachedObject.Children)
            {
                var childAdapter = adapterIndex.NodeAdapters.TryGetValue(child.Id, out var existingChild)
                    ? existingChild
                    : new SceneNodeAdapter(child);

                await AttachToParentAsync(adapter, childAdapter).ConfigureAwait(true);
                usedNodeIds.Add(child.Id);
            }
        }
    }

    private static async Task AttachToParentAsync(TreeItemAdapter parent, TreeItemAdapter child)
    {
        if (ReferenceEquals(child.Parent, parent))
        {
            return;
        }

        await DetachAdapterAsync(child).ConfigureAwait(true);

        switch (parent)
        {
            case SceneAdapter sceneAdapter:
                await sceneAdapter.AddChildAsync(child).ConfigureAwait(true);
                break;

            case FolderAdapter folderAdapter:
                if (child is FolderAdapter f)
                {
                    folderAdapter.AddFolder(f);
                }
                else
                {
                    folderAdapter.AddContent(child);
                }

                break;

            case SceneNodeAdapter sceneNodeAdapter:
                if (child is FolderAdapter folder)
                {
                    sceneNodeAdapter.AddFolder(folder);
                }
                else
                {
                    sceneNodeAdapter.AddContent(child);
                }

                break;
        }
    }

    private static async Task DetachAdapterAsync(TreeItemAdapter adapter)
    {
        switch (adapter.Parent)
        {
            case FolderAdapter folder:
                if (adapter is FolderAdapter f)
                {
                    _ = folder.RemoveFolder(f);
                }
                else
                {
                    _ = folder.RemoveContent(adapter);
                }

                break;

            case SceneNodeAdapter sceneNode:
                if (adapter is FolderAdapter folderChild)
                {
                    _ = sceneNode.RemoveFolder(folderChild);
                }
                else
                {
                    _ = sceneNode.RemoveContent(adapter);
                }

                break;

            case TreeItemAdapter parent:
                _ = await parent.RemoveChildAsync(adapter).ConfigureAwait(true);
                break;
        }
    }

    private static async Task<AdapterIndex> BuildAdapterIndexAsync(SceneAdapter root)
    {
        var index = new AdapterIndex();

        var rootChildren = await root.Children.ConfigureAwait(true);
        var stack = new Stack<ITreeItem>((IEnumerable<ITreeItem>?)rootChildren ?? Array.Empty<ITreeItem>());

        while (stack.Count > 0)
        {
            var item = stack.Pop();

            if (item is SceneNodeAdapter sna)
            {
                index.NodeAdapters[sna.AttachedObject.Id] = sna;
                index.NodesById[sna.AttachedObject.Id] = sna.AttachedObject;
                index.AllAdapters.Add(sna);
            }
            else if (item is FolderAdapter folder)
            {
                index.FolderAdapters[folder.Id] = folder;
                index.AllAdapters.Add(folder);
            }
            else if (item is SceneAdapter sa)
            {
                index.AllAdapters.Add(sa);
            }

            if (item is TreeItemAdapter adapter)
            {
                var children = await adapter.Children.ConfigureAwait(true);
                if (children is not null)
                {
                    foreach (var child in children)
                    {
                        stack.Push(child);
                    }
                }
            }
        }

        // Seed node cache from scene graph to allow adapter reuse even if not realized.
        foreach (var node in root.AttachedObject.AllNodes)
        {
            if (!index.NodesById.ContainsKey(node.Id))
            {
                index.NodesById[node.Id] = node;
            }
        }

        return index;
    }

    private sealed class AdapterIndex
    {
        public Dictionary<Guid, SceneNodeAdapter> NodeAdapters { get; } = [];

        public Dictionary<Guid, FolderAdapter> FolderAdapters { get; } = [];

        public Dictionary<Guid, SceneNode> NodesById { get; } = [];

        public List<TreeItemAdapter> AllAdapters { get; } = [];
    }

    private static IList<ExplorerEntryData> NormalizeLayout(IList<ExplorerEntryData> layout)
    {
        return layout.Select(NormalizeEntry).ToList();

        static ExplorerEntryData NormalizeEntry(ExplorerEntryData entry)
        {
            // We allow children for both Folders and Nodes (since Nodes can now contain Folders in the layout)
            var normalizedChildren = entry.Children is null
                ? []
                : entry.Children.Select(NormalizeEntry).ToList();

            return entry with { Children = normalizedChildren };
        }
    }

    private static bool IsNodeUnderFolderLineage(IList<ExplorerEntryData> layout, Guid nodeId, ExplorerEntryData folder, Scene scene)
    {
        // Lineage constraint: The folder must be visually located under the same Scene Node that is the
        // actual parent of the node we are moving.
        // This ensures that the "Overlay" layout respects the hard Scene Graph hierarchy.

        // 1. Find the node in the scene to check its REAL parent.
        var node = FindNodeById(scene, nodeId);
        if (node is null)
        {
            // If the node is not in the scene, we can't validate lineage.
            // This might happen if we are adding a node that hasn't been synced yet?
            // But SceneExplorerService calls Mutator (Sync) BEFORE Organizer.
            return false;
        }

        // 2. Find the path to the folder in the layout
        var folderPath = FindEntryPath(layout, folder);
        if (folderPath.Count == 0)
        {
            return false; // Folder not in layout
        }

        // 3. Find the nearest "Node" ancestor in the folder's path
        // Iterate backwards from folder's parent (folder is at Last index)
        Guid? expectedParentId = null;
        for (int i = folderPath.Count - 2; i >= 0; i--)
        {
            var entry = folderPath[i];
            if (TypeComparer.Equals(entry.Type, "Node") && entry.NodeId.HasValue)
            {
                expectedParentId = entry.NodeId.Value;
                break;
            }
        }

        // 4. Validate
        if (expectedParentId.HasValue)
        {
            return node.Parent?.Id == expectedParentId.Value;
        }
        else
        {
            // Folder is at root level (or only under other folders at root)
            // Node must be a root node
            return node.Parent is null;
        }
    }

    private static IList<ExplorerEntryData> FindEntryPath(IList<ExplorerEntryData>? entries, ExplorerEntryData target, IList<ExplorerEntryData>? current = null)
    {
        var path = current is null ? [] : new List<ExplorerEntryData>(current);
        if (entries is null)
        {
            return path;
        }

        foreach (var entry in entries)
        {
            if (ReferenceEquals(entry, target))
            {
                path.Add(entry);
                return path;
            }

            if (entry.Children is null || entry.Children.Count == 0)
            {
                continue;
            }

            path.Add(entry);
            var found = FindEntryPath(entry.Children, target, path);
            if (found.Count > 0 && ReferenceEquals(found[^1], target))
            {
                return found;
            }

            path.RemoveAt(path.Count - 1);
        }

        return current ?? [];
    }

    private static IList<ExplorerEntryData> FindNodePath(IList<ExplorerEntryData>? entries, Guid nodeId, IList<ExplorerEntryData>? current = null)
    {
        var path = current is null ? [] : new List<ExplorerEntryData>(current);
        if (entries is null)
        {
            return path;
        }

        foreach (var entry in entries)
        {
            if (TypeComparer.Equals(entry.Type, "Node") && entry.NodeId == nodeId)
            {
                path.Add(entry);
                return path;
            }

            if (entry.Children is null || entry.Children.Count == 0)
            {
                continue;
            }

            path.Add(entry);
            var found = FindNodePath(entry.Children, nodeId, path);
            if (found.Count > 0 && found[^1].NodeId == nodeId)
            {
                return found;
            }

            path.RemoveAt(path.Count - 1);
        }

        return current ?? [];
    }

    private static bool ContainsEntry(IList<ExplorerEntryData>? entries, ExplorerEntryData target)
    {
        if (entries is null)
        {
            return false;
        }

        foreach (var entry in entries)
        {
            if (ReferenceEquals(entry, target))
            {
                return true;
            }

            if (entry.Children is not null && entry.Children.Count > 0 && ContainsEntry(entry.Children, target))
            {
                return true;
            }
        }

        return false;
    }

    private static IList<ExplorerEntryData>? FindHighestCommonAncestor(IList<ExplorerEntryData> layout, HashSet<Guid> selectedNodeIds)
    {
        // If only one node, use its parent list as placement parent
        if (selectedNodeIds.Count == 1)
        {
            var singleId = selectedNodeIds.First();
            var (_, parentList) = FindEntryWithParent(layout, singleId);
            return parentList;
        }

        var paths = selectedNodeIds.Select(id => FindNodePath(layout, id)).Where(p => p.Count > 0).ToList();
        if (paths.Count == 0)
        {
            return null;
        }

        // Find deepest shared ancestor entry among all paths
        ExplorerEntryData? hcaEntry = null;
        var minLength = paths.Min(p => p.Count);
        for (var i = 0; i < minLength; i++)
        {
            var candidate = paths[0][i];
            if (paths.All(p => ReferenceEquals(p[i], candidate)))
            {
                hcaEntry = candidate;
            }
            else
            {
                break;
            }
        }

        if (hcaEntry is null)
        {
            return null;
        }

        // Return the children list of the HCA so folder can be added under it
        return hcaEntry.Children;
    }

    private static (ExplorerEntryData? entry, IList<ExplorerEntryData>? parent) FindEntryWithParent(IList<ExplorerEntryData>? entries, Guid nodeId, IList<ExplorerEntryData>? parent = null)
    {
        if (entries is null)
        {
            return (null, null);
        }

        foreach (var e in entries)
        {
            if (TypeComparer.Equals(e.Type, "Node") && e.NodeId == nodeId)
            {
                return (e, parent ?? entries);
            }

            var found = FindEntryWithParent(e.Children, nodeId, e.Children);
            if (found.entry is not null)
            {
                return found;
            }
        }

        return (null, null);
    }

    public HashSet<Guid> FilterTopLevelSelectedNodeIds(HashSet<Guid> selectedIds, Scene scene)
    {
        var topLevel = new HashSet<Guid>();
        foreach (var id in selectedIds)
        {
            if (HasAncestorInSelection(id, selectedIds, scene))
            {
                continue;
            }

            _ = topLevel.Add(id);
        }

        return topLevel;
    }

    private static bool HasAncestorInSelection(Guid nodeId, HashSet<Guid> selectedIds, Scene scene)
    {
        var node = FindNodeById(scene, nodeId);
        var current = node?.Parent;
        while (current is not null)
        {
            if (selectedIds.Contains(current.Id))
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
    }

    private static SceneNode? FindNodeById(Scene scene, Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            if (root.Id == nodeId)
            {
                return root;
            }

            var match = root.Descendants().FirstOrDefault(child => child.Id == nodeId);
            if (match is not null)
            {
                return match;
            }
        }

        return null;
    }
}
