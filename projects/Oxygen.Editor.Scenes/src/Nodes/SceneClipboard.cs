// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>A copied selection of nodes and explorer folders, as data that keeps its explorer grouping.</summary>
/// <remarks>
/// Pasting builds one batch. A copy inserts new nodes and folders; a cut moves the captured items, which must still be
/// in the scene and in one scope (the root or one node's children). Either way the items keep their grouping, folders
/// their expanded state, and land together at the target.
/// </remarks>
public sealed record SceneClipboard
{
    /// <summary>Initializes a new instance of the <see cref="SceneClipboard"/> class.</summary>
    /// <param name="entries">The selected explorer entries, in order, with the folders and entries under them.</param>
    /// <param name="nodes">The top-level node subtrees the entries hold, with their local transforms.</param>
    /// <param name="worldNodes">The same subtrees with their world transforms, or <see langword="null"/> when one is not representable.</param>
    public SceneClipboard(IReadOnlyList<ExplorerEntryData> entries, IReadOnlyList<SceneNodeData> nodes, IReadOnlyList<SceneNodeData>? worldNodes)
    {
        ArgumentNullException.ThrowIfNull(entries);
        ArgumentNullException.ThrowIfNull(nodes);
        this.Entries = [.. entries.Select(SerializationCopies.Copy)];
        this.Nodes = [.. nodes.Select(SerializationCopies.Copy)];
        this.WorldNodes = worldNodes is null ? null : [.. worldNodes.Select(SerializationCopies.Copy)];
    }

    /// <summary>Gets the selected explorer entries.</summary>
    public IReadOnlyList<ExplorerEntryData> Entries { get; }

    /// <summary>Gets the top-level node subtrees, with their local transforms.</summary>
    public IReadOnlyList<SceneNodeData> Nodes { get; }

    /// <summary>Gets the top-level node subtrees with their world transforms, if every one is representable.</summary>
    public IReadOnlyList<SceneNodeData>? WorldNodes { get; }

    /// <summary>Captures selected nodes and folders. A selected item inside another selected item goes with it.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="nodes">The selected nodes; missing ids are skipped.</param>
    /// <param name="folders">The selected folders; missing ids are skipped.</param>
    /// <returns>The clipboard, or <see langword="null"/> when nothing selected exists.</returns>
    public static SceneClipboard? Capture(Scene scene, IList<ExplorerEntryData> layout, IReadOnlyCollection<NodeId> nodes, IReadOnlyCollection<FolderId> folders)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(folders);
        var selected = nodes.Select(LayoutEntryId.Of).Concat(folders.Select(LayoutEntryId.Of)).ToHashSet();
        var entries = new List<ExplorerEntryData>();
        Collect(layout, selected, entries);
        if (entries.Count == 0)
        {
            return null;
        }

        var roots = TopLevelNodes(entries).Select(id => scene.AllNodes.FirstOrDefault(node => node.Id == id)).OfType<SceneNode>().ToList();
        var world = roots.Select(WorldSnapshot).OfType<SceneNodeData>().ToList();
        return new SceneClipboard(entries, [.. roots.Select(static node => node.Dehydrate())], world.Count == roots.Count ? world : null);
    }

    /// <summary>Makes a clipboard from node data, such as nodes copied from another scene; each root gets a plain entry.</summary>
    /// <param name="nodes">The node subtrees, with their local transforms.</param>
    /// <returns>The clipboard.</returns>
    public static SceneClipboard Of(IReadOnlyList<SceneNodeData> nodes)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        return new SceneClipboard([.. nodes.Select(EntryFor)], nodes, worldNodes: null);
    }

    /// <summary>Builds the batch that pastes the clipboard.</summary>
    /// <param name="scene">The scene to paste into.</param>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="target">Where the items go.</param>
    /// <param name="cut">Whether to move the captured items rather than insert copies.</param>
    /// <param name="preserveWorld">Whether the items keep their world transforms rather than their local ones.</param>
    /// <param name="edit">The batch, when it can be built.</param>
    /// <param name="error">Why it cannot be built, otherwise.</param>
    /// <returns>Whether the batch was built.</returns>
    public bool TryPaste(
        Scene scene,
        IList<ExplorerEntryData> layout,
        PasteTarget target,
        bool cut,
        bool preserveWorld,
        [NotNullWhen(true)] out SceneEditBatch? edit,
        [NotNullWhen(false)] out SceneDiagnostic? error)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(target);
        edit = null;
        error = Destination(scene, layout, target, out var scope, out var destination);
        if (error is not null)
        {
            return false;
        }

        var edits = new List<ISceneEdit>();
        error = cut ? this.CutItems(layout, out var items) : this.CopyItems(scene, layout, destination, preserveWorld, edits, out items);
        if (error is not null)
        {
            return false;
        }

        var roots = TopLevelNodes(items).ToList();
        var folders = Folders(items).ToList();
        LayoutEntryId[] top = [.. items.Select(SceneLayout.IdOf)];
        if (roots.Count > 0)
        {
            edits.Add(new ReparentNodes(roots, scope, preserveWorld, target.After));
        }

        // The roots' entries left their folders; bring the folders to the target, then put their entries back.
        if (cut && folders.Count > 0)
        {
            edits.Add(new MoveLayoutEntries([.. top.Where(static id => id.Folder is not null)], destination with { Index = 0 }));
        }

        edits.AddRange(folders.Where(static folder => folder.Children is { Count: > 0 }).Select(folder =>
            new MoveLayoutEntries([.. folder.Children!.Select(SceneLayout.IdOf)], LayoutPosition.InFolder(folder.FolderId!.Value, 0))));

        var rest = (SceneLayout.ContainerOf(layout, destination) ?? []).Select(SceneLayout.IdOf).Except(top).ToList();
        var anchor = target.After is { } after ? rest.IndexOf(LayoutEntryId.Of(after)) : -1;
        edits.Add(new MoveLayoutEntries(top, destination with { Index = anchor >= 0 ? anchor + 1 : rest.Count }));
        edit = new SceneEditBatch(cut ? "Move Items" : "Paste", edits);
        return true;
    }

    // The scene parent and the layout container the items go to.
    private static SceneDiagnostic? Destination(Scene scene, IList<ExplorerEntryData> layout, PasteTarget target, out NodeId? scope, out LayoutPosition destination)
    {
        scope = target.Parent;
        destination = LayoutPosition.AtRoot(0);
        if (target.After is { } after)
        {
            if (scene.AllNodes.FirstOrDefault(node => node.Id == after) is not { } anchor)
            {
                return SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The node to paste after no longer exists.", after);
            }

            if (target.Parent is { } parent && anchor.Hierarchy.Parent?.Id != parent)
            {
                return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "The node to paste after is not under the target parent.", after);
            }

            scope = anchor.Hierarchy.Parent?.Id;
        }

        if (target.Folder is { } folder)
        {
            var (found, folderScope) = SceneLayout.ScopeOfEntry(layout, LayoutEntryId.Of(folder));
            if (!found)
            {
                return SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The target folder no longer exists.");
            }

            if ((target.Parent is not null || target.After is not null) && folderScope != scope)
            {
                return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "The target folder is not in the target parent's scope.");
            }

            scope = folderScope;
            destination = LayoutPosition.InFolder(folder, 0);
            return null;
        }

        if (scope is { } node)
        {
            if (scene.AllNodes.All(candidate => candidate.Id != node))
            {
                return SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The target parent no longer exists.", node);
            }

            destination = LayoutPosition.UnderNode(node, 0);
        }

        return null;
    }

    // Selected entries, in layout order, without descending into a selected entry.
    private static void Collect(IList<ExplorerEntryData> entries, HashSet<LayoutEntryId> selected, List<ExplorerEntryData> found)
    {
        foreach (var entry in entries)
        {
            if (selected.Contains(SceneLayout.IdOf(entry)))
            {
                found.Add(entry);
            }
            else if (entry.Children is { } children)
            {
                Collect(children, selected, found);
            }
        }
    }

    // The node entries reached through folders only: the nodes whose scene parent is not part of the items.
    private static IEnumerable<NodeId> TopLevelNodes(IEnumerable<ExplorerEntryData> entries)
        => entries.SelectMany(entry => SceneLayout.IsFolder(entry) ? TopLevelNodes(entry.Children ?? []) : SceneLayout.IsNode(entry) ? [entry.NodeId!.Value] : []);

    // The folders reached through folders only, outer folders first.
    private static IEnumerable<ExplorerEntryData> Folders(IEnumerable<ExplorerEntryData> entries)
        => entries.Where(SceneLayout.IsFolder).SelectMany(folder => new[] { folder }.Concat(Folders(folder.Children ?? [])));

    private static SceneNodeData? WorldSnapshot(SceneNode node)
    {
        if (!SceneTransformMath.TryPreserveWorldLocal(node, newParent: null, out var position, out var rotation, out var scale))
        {
            return null;
        }

        var data = node.Dehydrate();
        return data with
        {
            Components = [.. data.Components.Select(component => component is TransformData transform
                ? transform with { Position = position, Rotation = rotation, Scale = scale }
                : component)],
        };
    }

    private static ExplorerEntryData EntryFor(SceneNodeData node)
        => new()
        {
            Type = SceneLayout.NodeType,
            NodeId = node.Id,
            Children = node.Children is not { Count: > 0 } children ? null : [.. children.Select(EntryFor)],
        };

    // A cut moves the items as the layout has them now, which may differ from the capture. They come from one scope; a
    // cycle is the built batch's to refuse.
    private SceneDiagnostic? CutItems(IList<ExplorerEntryData> layout, out List<ExplorerEntryData> items)
    {
        items = [with(this.Entries.Count)];
        (bool Known, NodeId? Scope) source = default;
        foreach (var id in this.Entries.Select(SceneLayout.IdOf))
        {
            if (SceneLayout.Find(layout, id) is not { } found)
            {
                return id.Node is { } node
                    ? SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "A cut node no longer exists.", node)
                    : SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "A cut folder no longer exists.");
            }

            var (_, scope) = SceneLayout.ScopeOfEntry(layout, id);
            if (source.Known && source.Scope != scope)
            {
                return SceneDiagnostic.Error(SceneEditCodes.LayoutScopeMismatch, "Cut items must come from one node's children or from the root.", id.Node);
            }

            source = (true, scope);
            items.Add(found.Entry);
        }

        return null;
    }

    // A copy creates its folders at the target and inserts its nodes at the root; the batch then moves them in place.
    private SceneDiagnostic? CopyItems(Scene scene, IList<ExplorerEntryData> layout, LayoutPosition destination, bool preserveWorld, List<ISceneEdit> edits, out List<ExplorerEntryData> items)
    {
        items = [];
        if (preserveWorld && this.WorldNodes is null)
        {
            return SceneDiagnostic.Error(SceneEditCodes.TransformUnrepresentable, "A copied node's world transform cannot be represented.");
        }

        var copies = SceneNodeCopies.Copy(scene, preserveWorld ? this.WorldNodes! : this.Nodes);
        items = [.. this.Entries.Select(entry => SceneNodeCopies.RemapEntry(entry, copies.Ids))];
        var data = copies.Roots.ToDictionary(static root => root.Id);
        var rootNodes = scene.RootNodes.Count;
        var rootEntries = layout.Count;
        var inserts = new List<NodeInsert>();
        foreach (var entry in NodeEntries(items))
        {
            if (!data.TryGetValue(entry.NodeId!.Value, out var node))
            {
                return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "The clipboard has an entry without its node.");
            }

            inserts.Add(new NodeInsert(node, new NodePosition(Parent: null, rootNodes + inserts.Count), LayoutPosition.AtRoot(rootEntries + inserts.Count), entry));
        }

        edits.AddRange(Folders(items).Select(folder => new CreateFolder(folder.FolderId!.Value, destination with { Index = 0 }, folder.Name ?? "Folder", folder.IsExpanded)));
        if (inserts.Count > 0)
        {
            edits.Add(new InsertNodes(inserts, "Paste"));
        }

        return null;

        static IEnumerable<ExplorerEntryData> NodeEntries(IEnumerable<ExplorerEntryData> entries)
            => entries.SelectMany(entry => SceneLayout.IsFolder(entry) ? NodeEntries(entry.Children ?? []) : SceneLayout.IsNode(entry) ? [entry] : []);
    }
}
