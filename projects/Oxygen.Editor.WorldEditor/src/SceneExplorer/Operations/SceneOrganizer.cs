// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Default implementation of <see cref="ISceneOrganizer" /> that mutates only <see cref="Scene.ExplorerLayout" />.
/// </summary>
public sealed class SceneOrganizer : ISceneOrganizer
{
    private readonly ILogger<SceneOrganizer> logger;

    public SceneOrganizer(ILogger<SceneOrganizer> logger)
    {
        this.logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    private static readonly StringComparer TypeComparer = StringComparer.OrdinalIgnoreCase;

    public LayoutChangeRecord CreateFolderFromSelection(HashSet<Guid> selectedNodeIds, Scene scene, SceneAdapter sceneAdapter)
    {
        ArgumentNullException.ThrowIfNull(selectedNodeIds);
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(sceneAdapter);
        _ = sceneAdapter; // reserved for future context needs

        if (selectedNodeIds.Count == 0)
        {
            throw new InvalidOperationException("Cannot create folder with empty selection.");
        }

        var previousLayout = CloneLayout(scene.ExplorerLayout) ?? BuildLayoutFromRootNodes(scene);
        var layout = scene.ExplorerLayout?.ToList() ?? BuildLayoutFromRootNodes(scene);

        // Determine placement: highest common ancestor among selected nodes' layout lineage
        var selectedIdsTopLevel = FilterTopLevelSelectedNodeIds(selectedNodeIds, scene);
        var placementParent = FindHighestCommonAncestor(layout, selectedIdsTopLevel);

        var movedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(placementParent ?? layout, selectedIdsTopLevel, movedEntries);

        var folderId = Guid.NewGuid();
        var folderEntry = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = folderId,
            Name = "New Folder",
            Children = movedEntries.Count > 0 ? movedEntries : new List<ExplorerEntryData>(),
        };

        var targetList = placementParent ?? layout;
        targetList.Insert(0, folderEntry);
        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "CreateFolderFromSelection",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            NewFolder: folderEntry,
            ModifiedFolders: new List<ExplorerEntryData> { folderEntry });
    }

    public LayoutChangeRecord MoveNodeToFolder(Guid nodeId, Guid folderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = CloneLayout(scene.ExplorerLayout);
        var layout = RequireLayout(scene);

        var (folderEntry, folderParentList) = FindFolderEntryWithParent(layout, folderId);
        if (folderEntry is null)
        {
            throw new InvalidOperationException($"Folder '{folderId}' not found.");
        }

        if (!IsNodeUnderFolderLineage(layout, nodeId, folderEntry))
        {
            throw new InvalidOperationException("Cannot move node to a folder outside its lineage.");
        }

        // Remove node from any existing location
        var removedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, new HashSet<Guid> { nodeId }, removedEntries);

        var nodeEntry = removedEntries.FirstOrDefault() ?? new ExplorerEntryData { Type = "Node", NodeId = nodeId };

        folderEntry.Children!.Add(nodeEntry);

        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "MoveNodeToFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: new List<ExplorerEntryData> { folderEntry },
            ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
    }

    public LayoutChangeRecord RemoveNodeFromFolder(Guid nodeId, Guid folderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = CloneLayout(scene.ExplorerLayout);
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
                ModifiedFolders: new List<ExplorerEntryData> { folderEntry },
                ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
        }

        var nodeEntry = folderEntry.Children.FirstOrDefault(e => TypeComparer.Equals(e.Type, "Node") && e.NodeId == nodeId);
        if (nodeEntry is not null)
        {
            _ = folderEntry.Children.Remove(nodeEntry);
        }

        scene.ExplorerLayout = layout;

        return new LayoutChangeRecord(
            OperationName: "RemoveNodeFromFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: new List<ExplorerEntryData> { folderEntry },
            ParentLists: folderParentList is null ? null : new List<IList<ExplorerEntryData>> { folderParentList });
    }

    public LayoutChangeRecord MoveFolderToParent(Guid folderId, Guid? newParentFolderId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = CloneLayout(scene.ExplorerLayout);
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

        return new LayoutChangeRecord(
            OperationName: "MoveFolderToParent",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: new List<ExplorerEntryData> { folderEntry });
    }

    public LayoutChangeRecord RemoveFolder(Guid folderId, bool promoteChildrenToParent, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var previousLayout = CloneLayout(scene.ExplorerLayout);
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

        return new LayoutChangeRecord(
            OperationName: "RemoveFolder",
            PreviousLayout: previousLayout,
            NewLayout: layout,
            ModifiedFolders: new List<ExplorerEntryData> { folderEntry });
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
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.Children.Count == 0)
                {
                    entries.RemoveAt(i);
                }
            }
        }
    }

    public IList<ExplorerEntryData>? CloneLayout(IList<ExplorerEntryData>? layout)
    {
        if (layout is null)
        {
            return null;
        }

        return layout.Select(CloneEntry).ToList();
    }

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

    public IList<ExplorerEntryData> BuildFolderOnlyLayout(LayoutChangeRecord layoutChange)
    {
        Console.WriteLine($"DEBUG: BuildFolderOnlyLayout. NewFolder is null? {layoutChange.NewFolder is null}");
        if (layoutChange.NewFolder != null)
        {
            Console.WriteLine($"DEBUG: NewFolder ID: {layoutChange.NewFolder.FolderId}");
        }

        // Start from previous layout (so nodes remain outside the folder for first undo)
        var baseLayout = layoutChange.PreviousLayout is null
            ? new List<ExplorerEntryData>()
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

    private static IList<ExplorerEntryData> NormalizeLayout(IList<ExplorerEntryData> layout)
    {
        return layout.Select(NormalizeEntry).ToList();

        static ExplorerEntryData NormalizeEntry(ExplorerEntryData entry)
        {
            if (!TypeComparer.Equals(entry.Type, "Folder"))
            {
                return entry with { Children = null };
            }

            var normalizedChildren = entry.Children is null
                ? new List<ExplorerEntryData>()
                : entry.Children.Select(NormalizeEntry).ToList();

            return entry with { Children = normalizedChildren };
        }
    }

    private static bool IsNodeUnderFolderLineage(IList<ExplorerEntryData> layout, Guid nodeId, ExplorerEntryData folder)
    {
        // lineage constraint: folder must be within the ancestor chain of the node's parent.
        // Root nodes (no parent) may move into any root-level folder.
        var nodePath = FindNodePath(layout, nodeId);
        if (nodePath.Count == 0)
        {
            return false;
        }

        // Remove the node itself to get its ancestor chain
        nodePath.RemoveAt(nodePath.Count - 1);

        if (nodePath.Count == 0)
        {
            // Node is at root — allow any root-level folder
            return layout.Any(e => ReferenceEquals(e, folder));
        }

        // Folder must exist somewhere under the node's parent entry subtree
        var parent = nodePath.Last();
        return ContainsEntry(parent.Children, folder);
    }

    private static IList<ExplorerEntryData> FindNodePath(IList<ExplorerEntryData>? entries, Guid nodeId, IList<ExplorerEntryData>? current = null)
    {
        var path = current is null ? new List<ExplorerEntryData>() : new List<ExplorerEntryData>(current);
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
            if (found.Count > 0 && found.Last().NodeId == nodeId)
            {
                return found;
            }

            path.RemoveAt(path.Count - 1);
        }

        return current ?? new List<ExplorerEntryData>();
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
