// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Reads and changes a scene's explorer layout: its folders and node entries.</summary>
/// <remarks>
/// <para>
/// Every node has one entry, in the scope of its scene parent: the root for a root node, or under its parent's entry.
/// Folders group entries inside one scope. <see cref="Complete(Scene, IList{ExplorerEntryData})"/> gives a scene's layout an entry
/// for every node, so the edits always find one; the scene document does it when it decodes.
/// </para>
/// <para>
/// The read methods never change the layout, so validation can use them.
/// </para>
/// </remarks>
internal static class SceneLayout
{
    /// <summary>The entry type of a node's entry.</summary>
    public const string NodeType = "Node";

    /// <summary>The entry type of a folder.</summary>
    public const string FolderType = "Folder";

    /// <summary>Completes a scene's layout: an entry for every node and a child list for every folder.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="layout">The scene's layout, completed in place.</param>
    public static void Complete(Scene scene, IList<ExplorerEntryData> layout)
    {
        foreach (var folder in Folders(layout))
        {
            _ = folder.EnsureChildren();
        }

        foreach (var root in scene.RootNodes)
        {
            Complete(layout, root, layout);
        }
    }

    /// <summary>Tests whether an entry is a node's entry.</summary>
    /// <param name="entry">The entry.</param>
    /// <returns>Whether it is.</returns>
    public static bool IsNode(ExplorerEntryData entry) => string.Equals(entry.Type, NodeType, StringComparison.OrdinalIgnoreCase) && entry.NodeId is not null;

    /// <summary>Tests whether an entry is a folder.</summary>
    /// <param name="entry">The entry.</param>
    /// <returns>Whether it is.</returns>
    public static bool IsFolder(ExplorerEntryData entry) => string.Equals(entry.Type, FolderType, StringComparison.OrdinalIgnoreCase) && entry.FolderId is not null;

    /// <summary>Finds an entry and the list that holds it.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="id">The entry's id.</param>
    /// <returns>The entry and its list, or <see langword="null"/> when the layout has no such entry.</returns>
    public static (ExplorerEntryData Entry, IList<ExplorerEntryData> List)? Find(IList<ExplorerEntryData>? layout, LayoutEntryId id)
        => Find(layout, entry => id.Node is { } node ? IsNode(entry) && entry.NodeId == node : IsFolder(entry) && entry.FolderId == id.Folder);

    /// <summary>Gets the id of an entry.</summary>
    /// <param name="entry">The entry.</param>
    /// <returns>Its id.</returns>
    public static LayoutEntryId IdOf(ExplorerEntryData entry)
        => IsNode(entry) ? LayoutEntryId.Of(entry.NodeId!.Value) : LayoutEntryId.Of(entry.FolderId!.Value);

    /// <summary>Gets the list a position names, or <see langword="null"/> when its container is not in the layout.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="position">The position.</param>
    /// <returns>The container's entries; a node's entry without children gives an empty list it does not hold yet.</returns>
    public static IList<ExplorerEntryData>? ContainerOf(IList<ExplorerEntryData> layout, LayoutPosition position)
    {
        if (position.Folder is null && position.Node is null)
        {
            return layout;
        }

        var owner = position.Folder is { } folder ? LayoutEntryId.Of(folder) : LayoutEntryId.Of(position.Node!.Value);
        return Find(layout, owner) is { } found ? found.Entry.Children ?? [] : null;
    }

    /// <summary>Gets the list a position names, creating a node entry's child list when it has none.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="position">The position, whose container is in the layout.</param>
    /// <returns>The container's entries.</returns>
    public static IList<ExplorerEntryData> RequireContainer(IList<ExplorerEntryData> layout, LayoutPosition position)
        => position.Folder is { } folder
            ? Find(layout, LayoutEntryId.Of(folder))!.Value.Entry.EnsureChildren()
            : position.Node is { } node
            ? Find(layout, LayoutEntryId.Of(node))!.Value.Entry.EnsureChildren()
            : layout;

    /// <summary>Checks that a position's container is in the layout and its index in range.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="position">The position.</param>
    /// <returns>Why the position is wrong, or <see langword="null"/> when it is right.</returns>
    public static SceneDiagnostic? Check(IList<ExplorerEntryData> layout, LayoutPosition position)
        => ContainerOf(layout, position) is not { } container
            ? SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The layout destination no longer exists.")
            : position.Index < 0 || position.Index > container.Count
            ? SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The layout position is outside its container.")
            : null;

    /// <summary>Gets the scene scope of a position: the node whose children it groups, or the root.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="position">The position.</param>
    /// <returns>Whether the position's container is in the layout, and its scope.</returns>
    public static (bool Found, NodeId? Scope) ScopeOf(IList<ExplorerEntryData> layout, LayoutPosition position)
        => position.Folder is { } folder
            ? ScopeOfEntry(layout, LayoutEntryId.Of(folder))
            : position.Node is { } node
            ? (Find(layout, LayoutEntryId.Of(node)) is not null, node)
            : (true, null);

    /// <summary>Gets the scene scope an entry lives in.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="id">The entry's id.</param>
    /// <returns>Whether the entry is in the layout, and its scope.</returns>
    public static (bool Found, NodeId? Scope) ScopeOfEntry(IList<ExplorerEntryData> layout, LayoutEntryId id)
        => PathTo(layout, id) is not { } path
            ? (false, null)
            : (true, path.Take(path.Count - 1).LastOrDefault(IsNode)?.NodeId);

    /// <summary>Gets an entry's position.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="id">The entry's id.</param>
    /// <returns>Its position, or <see langword="null"/> when the layout has no such entry.</returns>
    public static LayoutPosition? PositionOf(IList<ExplorerEntryData> layout, LayoutEntryId id)
    {
        if (PathTo(layout, id) is not { } path)
        {
            return null;
        }

        var owner = path.Count > 1 ? path[^2] : null;
        var list = owner?.Children ?? layout;
        var index = list.IndexOf(path[^1]);
        return owner is null
            ? LayoutPosition.AtRoot(index)
            : IsFolder(owner) ? LayoutPosition.InFolder(owner.FolderId!.Value, index) : LayoutPosition.UnderNode(owner.NodeId!.Value, index);
    }

    /// <summary>Tests whether an entry is inside another one.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="inner">The entry that may be inside.</param>
    /// <param name="outer">The entry that may contain it.</param>
    /// <returns>Whether <paramref name="inner"/> is under <paramref name="outer"/>.</returns>
    public static bool IsInside(IList<ExplorerEntryData> layout, LayoutEntryId inner, LayoutEntryId outer)
        => PathTo(layout, inner) is { } path && path.Take(path.Count - 1).Any(entry => IdOf(entry) == outer);

    /// <summary>Gets the node entries an entry holds, itself included, at any depth.</summary>
    /// <param name="entry">The entry.</param>
    /// <returns>The node ids.</returns>
    public static IEnumerable<NodeId> NodesIn(ExplorerEntryData entry)
    {
        if (IsNode(entry))
        {
            yield return entry.NodeId!.Value;
        }

        foreach (var child in entry.Children ?? [])
        {
            foreach (var node in NodesIn(child))
            {
                yield return node;
            }
        }
    }

    /// <summary>Gets the node entries of a container that belong to its scope, through its folders.</summary>
    /// <param name="entries">The container's entries.</param>
    /// <returns>The nodes whose entries are directly in the container or in its folders.</returns>
    public static IEnumerable<NodeId> ScopeNodes(IEnumerable<ExplorerEntryData> entries)
    {
        foreach (var entry in entries)
        {
            if (IsNode(entry))
            {
                yield return entry.NodeId!.Value;
            }
            else if (IsFolder(entry))
            {
                foreach (var node in ScopeNodes(entry.Children ?? []))
                {
                    yield return node;
                }
            }
        }
    }

    /// <summary>Gets every folder of a layout.</summary>
    /// <param name="layout">The layout.</param>
    /// <returns>The folders.</returns>
    public static IEnumerable<ExplorerEntryData> Folders(IEnumerable<ExplorerEntryData> layout)
    {
        foreach (var entry in layout)
        {
            if (IsFolder(entry))
            {
                yield return entry;
            }

            foreach (var folder in Folders(entry.Children ?? []))
            {
                yield return folder;
            }
        }
    }

    /// <summary>Takes entries out of the layout, then puts each at its place, so all places refer to one layout.</summary>
    /// <param name="layout">The layout.</param>
    /// <param name="placements">The entries, with their own objects, and where each goes.</param>
    /// <remarks>
    /// Places are applied by container and ascending index, so places captured from one layout restore it exactly.
    /// </remarks>
    public static void Place(IList<ExplorerEntryData> layout, IReadOnlyList<(ExplorerEntryData Entry, LayoutPosition At)> placements)
    {
        foreach (var (entry, _) in placements)
        {
            if (Find(layout, IdOf(entry)) is { } found)
            {
                _ = found.List.Remove(found.Entry);
            }
        }

        foreach (var (entry, at) in placements.OrderBy(static placement => placement.At.Index))
        {
            RequireContainer(layout, at).Insert(at.Index, entry);
        }
    }

    /// <summary>Drops the empty child lists entries left behind, so a node entry without children has none, as decoding writes it.</summary>
    /// <remarks>Folders keep their empty lists: a new folder has one.</remarks>
    /// <param name="entries">The layout or a container.</param>
    public static void Prune(IList<ExplorerEntryData> entries)
    {
        for (var i = 0; i < entries.Count; i++)
        {
            if (entries[i].Children is not { } children)
            {
                continue;
            }

            if (children.Count == 0 && IsNode(entries[i]))
            {
                entries[i] = entries[i] with { Children = null };
            }
            else
            {
                Prune(children);
            }
        }
    }

    /// <summary>Builds the entry of a node and of its subtree, for a node that has none.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The entry.</returns>
    public static ExplorerEntryData EntryFor(SceneNode node)
        => new()
        {
            Type = NodeType,
            NodeId = node.Id,
            Children = node.Hierarchy.Children.Count == 0 ? null : [.. node.Hierarchy.Children.Select(EntryFor)],
        };

    private static void Complete(IList<ExplorerEntryData> layout, SceneNode node, IList<ExplorerEntryData> scope)
    {
        if (Find(layout, LayoutEntryId.Of(node.Id)) is not { } found)
        {
            scope.Add(EntryFor(node));
            return;
        }

        foreach (var child in node.Hierarchy.Children)
        {
            Complete(layout, child, found.Entry.EnsureChildren());
        }
    }

    private static (ExplorerEntryData Entry, IList<ExplorerEntryData> List)? Find(IList<ExplorerEntryData>? entries, Func<ExplorerEntryData, bool> match)
    {
        foreach (var entry in entries ?? [])
        {
            if (match(entry))
            {
                return (entry, entries!);
            }

            if (Find(entry.Children, match) is { } found)
            {
                return found;
            }
        }

        return null;
    }

    private static List<ExplorerEntryData>? PathTo(IList<ExplorerEntryData>? entries, LayoutEntryId id)
    {
        foreach (var entry in entries ?? [])
        {
            if (IdOf(entry) == id)
            {
                return [entry];
            }

            if (PathTo(entry.Children, id) is { } path)
            {
                path.Insert(0, entry);
                return path;
            }
        }

        return null;
    }
}
