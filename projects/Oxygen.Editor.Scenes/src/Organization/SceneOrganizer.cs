// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.SceneExplorer.Operations;

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
    public LayoutChangeRecord MoveNodeToFolder(NodeId nodeId, FolderId folderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.LogSceneInfo("MoveNodeToFolder (Start)", scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, folderParentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null)
        {
            var allFolders = new List<FolderId>();
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

        scene.SetExplorerLayout(layout);

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
    public LayoutChangeRecord RemoveNodeFromFolder(NodeId nodeId, FolderId folderId, Scene scene)
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

        scene.SetExplorerLayout(layout);

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
    public LayoutChangeRecord RemoveNodeFromLayout(NodeId nodeId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var removedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, [nodeId], removedEntries);
        this.LogRemoveNodeFromLayout(removedEntries.Count, nodeId);

        scene.SetExplorerLayout(layout);

        return new LayoutChangeRecord(
            OperationName: "RemoveNodeFromLayout",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            NewFolder: null,
            ModifiedFolders: []);
    }

    private void EnsureNodeInLayout(IList<ExplorerEntryData> layout, NodeId nodeId, Scene scene)
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

            targetList = parentEntry.EnsureChildren();
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
    public LayoutChangeRecord CreateFolder(FolderId? parentFolderId, string name, Scene scene, FolderId? folderId = null, NodeId? parentNodeId = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.LogSceneInfo("CreateFolder (Start)", scene);
        this.LogCreateFolder(name, parentFolderId, parentNodeId, folderId);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var newFolderId = folderId ?? FolderId.New();
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

            parentEntry.EnsureChildren().Add(folderEntry);
        }
        else if (parentNodeId.HasValue)
        {
            this.EnsureNodeInLayout(layout, parentNodeId.Value, scene);
            var (parentEntry, _) = this.FindNodeEntryWithParent(layout, parentNodeId.Value);
            if (parentEntry == null)
            {
                throw new InvalidOperationException($"Parent node {parentNodeId} not found in layout.");
            }

            parentEntry.EnsureChildren().Add(folderEntry);
        }
        else
        {
            layout.Add(folderEntry);
        }

        scene.SetExplorerLayout(layout);
        this.LogSceneInfo("CreateFolder (End)", scene);

        return new LayoutChangeRecord(
            OperationName: "CreateFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            NewFolder: folderEntry,
            ModifiedFolders: [folderEntry]);
    }

    private (ExplorerEntryData? Entry, IList<ExplorerEntryData>? ParentList) FindNodeEntryWithParent(IList<ExplorerEntryData> entries, NodeId nodeId)
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
    public LayoutChangeRecord MoveFolderToParent(FolderId folderId, FolderId? newParentFolderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, parentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null || parentList is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        // Resolve and validate the destination list before removing the source, so a rejected
        // move (e.g. a missing target parent) leaves the authored layout unchanged.
        IList<ExplorerEntryData> targetList;
        if (newParentFolderId is null)
        {
            targetList = layout;
        }
        else
        {
            var (targetParent, _) = FindFolderEntryWithParent(layout, newParentFolderId.Value);
            if (targetParent is null)
            {
                throw new InvalidOperationException($"Target parent folder '{newParentFolderId}' not found.");
            }

            targetList = targetParent.Children!;
        }

        _ = parentList.Remove(folderEntry);
        targetList.Add(folderEntry);

        scene.SetExplorerLayout(layout);
        this.LogMoveFolderToParent(folderId, newParentFolderId);

        return new LayoutChangeRecord(
            OperationName: "MoveFolderToParent",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry]);
    }

    /// <inheritdoc />
    public LayoutChangeRecord MoveNodeToSiblingIndex(NodeId nodeId, FolderId? parentFolderId, NodeId? parentNodeId, int index, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = this.CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        IList<ExplorerEntryData> targetList;
        if (parentFolderId.HasValue)
        {
            var (folderEntry, _) = FindFolderEntryWithParent(layout, parentFolderId.Value);
            if (folderEntry is null)
            {
                throw new InvalidOperationException($"Folder '{parentFolderId}' not found.");
            }

            targetList = folderEntry.EnsureChildren();
        }
        else if (parentNodeId.HasValue)
        {
            this.EnsureNodeInLayout(layout, parentNodeId.Value, scene);
            var (parentNodeEntry, _) = this.FindNodeEntryWithParent(layout, parentNodeId.Value);
            if (parentNodeEntry is null)
            {
                throw new InvalidOperationException($"Node '{parentNodeId}' not found in layout.");
            }

            targetList = parentNodeEntry.EnsureChildren();
        }
        else
        {
            targetList = layout;
        }

        // Capture the node's current position within the destination list before removal so a
        // same-parent move can adjust the insertion index after the source entry is removed.
        var (currentEntry, currentParentList) = this.FindNodeEntryWithParent(layout, nodeId);
        var removedIndex = currentEntry is not null && currentParentList is not null && ReferenceEquals(currentParentList, targetList)
            ? currentParentList.IndexOf(currentEntry)
            : -1;

        var removed = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, [nodeId], removed);
        var nodeEntry = removed.FirstOrDefault() ?? new ExplorerEntryData { Type = "Node", NodeId = nodeId };

        var adjustedIndex = removedIndex >= 0 && removedIndex < index ? index - 1 : index;
        var insertIndex = Math.Clamp(adjustedIndex, 0, targetList.Count);
        targetList.Insert(insertIndex, nodeEntry);

        scene.SetExplorerLayout(layout);

        return new LayoutChangeRecord(
            OperationName: "MoveNodeToSiblingIndex",
            PreviousLayout: previousLayout,
            NewLayout: layout);
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
    public LayoutChangeRecord RemoveFolder(FolderId folderId, bool promoteChildrenToParent, Scene scene)
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

        scene.SetExplorerLayout(layout);
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
    public LayoutChangeRecord RenameFolder(FolderId folderId, string newName, Scene scene)
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
        scene.SetExplorerLayout(layout);
        this.LogRenameFolder(folderId, newName);

        return new LayoutChangeRecord(
            OperationName: "RenameFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: [folderEntry]);
    }

    private static IList<ExplorerEntryData> RequireLayout(Scene scene)
    {
        // The explorer layout is a lazy overlay over the scene graph: create it from the root
        // nodes on first use so folder operations work on a freshly loaded, layout-less scene.
        var layout = scene.ExplorerLayout ?? BuildLayoutFromRootNodes(scene);

        // Normalize to ensure folder entries always have a mutable children list for in-place updates.
        var normalized = NormalizeLayout(layout);
        scene.SetExplorerLayout(normalized);
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

    private static (ExplorerEntryData? entry, IList<ExplorerEntryData>? parent) FindFolderEntryWithParent(IList<ExplorerEntryData>? entries, FolderId id, IList<ExplorerEntryData>? parent = null)
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

    private static void RemoveEntriesForNodeIds(IList<ExplorerEntryData> entries, HashSet<NodeId> nodeIds, IList<ExplorerEntryData> removed)
    {
        for (var i = entries.Count - 1; i >= 0; i--)
        {
            var entry = entries[i];
            if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is NodeId nodeId && nodeIds.Contains(nodeId))
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
    public HashSet<FolderId> GetExpandedFolderIds(IList<ExplorerEntryData>? layout)
    {
        var expandedIds = new HashSet<FolderId>();
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

    private static ExplorerEntryData? FindFolderEntry(IList<ExplorerEntryData> entries, FolderId folderId)
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
    public void EnsureLayoutContainsNodes(Scene scene, IEnumerable<NodeId> nodeIds)
    {
        // Ensure layout exists
        var layout = scene.ExplorerLayout ?? BuildLayoutFromRootNodes(scene);
        scene.SetExplorerLayout(layout);

        foreach (var id in nodeIds)
        {
            if (LayoutContainsNode(layout, id))
            {
                continue;
            }

            layout.Add(new ExplorerEntryData { Type = "Node", NodeId = id });
        }

        static bool LayoutContainsNode(IList<ExplorerEntryData> layout, NodeId nodeId)
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
            FolderId = FolderId.New(),
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
            Children = entry.Children?.Select(CloneEntry).ToList(),
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

    private static IList<ExplorerEntryData> NormalizeLayout(IList<ExplorerEntryData> layout)
    {
        return layout.Select(NormalizeEntry).ToList();

        static ExplorerEntryData NormalizeEntry(ExplorerEntryData entry)
        {
            var normalizedChildren = entry.Children?.Select(NormalizeEntry).ToList();
            if (normalizedChildren is null && TypeComparer.Equals(entry.Type, "Folder"))
            {
                normalizedChildren = [];
            }

            return entry with { Children = normalizedChildren };
        }
    }

    private static bool IsNodeUnderFolderLineage(IList<ExplorerEntryData> layout, NodeId nodeId, ExplorerEntryData folder, Scene scene)
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
            // But SceneDocumentCommandService commits the graph mutation BEFORE the layout mutation.
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
        NodeId? expectedParentId = null;
        for (var i = folderPath.Count - 2; i >= 0; i--)
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

    private static IList<ExplorerEntryData> FindNodePath(IList<ExplorerEntryData>? entries, NodeId nodeId, IList<ExplorerEntryData>? current = null)
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

    private static IList<ExplorerEntryData>? FindHighestCommonAncestor(IList<ExplorerEntryData> layout, HashSet<NodeId> selectedNodeIds)
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

    private static (ExplorerEntryData? entry, IList<ExplorerEntryData>? parent) FindEntryWithParent(IList<ExplorerEntryData>? entries, NodeId nodeId, IList<ExplorerEntryData>? parent = null)
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

    public HashSet<NodeId> FilterTopLevelSelectedNodeIds(HashSet<NodeId> selectedIds, Scene scene)
    {
        var topLevel = new HashSet<NodeId>();
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

    private static bool HasAncestorInSelection(NodeId nodeId, HashSet<NodeId> selectedIds, Scene scene)
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

    private static SceneNode? FindNodeById(Scene scene, NodeId nodeId)
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
