// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Atomic mixed clipboard transactions, including folder-lineage scene reparenting.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc />
    public SceneValueCommandResult<SceneExplorerClipboard> CaptureExplorerClipboard(
        SceneDocumentCommandContext context, IReadOnlyList<Guid> nodeIds, IReadOnlyList<Guid> folderIds)
    {
        using var authoring = EnterAuthoring(context);
        var layout = this.CreateCompleteClipboardLayout(context.Scene);
        if (authoring is null || nodeIds.Any(id => FindNode(context.Scene, id) is null)
            || folderIds.Any(id => FindClipboardEntry(layout, nodeId: null, id) is null))
        {
            return this.ClipboardCaptureFailure("STALE_TARGET", "A selected item no longer belongs to the loaded scene.", context);
        }

        var entries = new List<ExplorerEntryData>();
        CollectSelectedEntries(layout, nodeIds.ToHashSet(), folderIds.ToHashSet(), entries);
        if (entries.Count == 0)
        {
            return this.ClipboardCaptureFailure("EMPTY_SELECTION", "Select scene nodes or folders to copy.", context);
        }

        var coveredIds = EnumerateClipboardEntries(entries).Where(static entry => entry.NodeId.HasValue)
            .Select(static entry => entry.NodeId!.Value).Distinct().ToArray();
        if (coveredIds.Any(id => FindNode(context.Scene, id) is null))
        {
            return this.ClipboardCaptureFailure("STALE_TARGET", "A selected folder references a node that no longer exists.", context);
        }

        var coveredSet = coveredIds.ToHashSet();
        var roots = coveredIds.Select(id => FindNode(context.Scene, id)!).Where(node =>
            !node.Ancestors().Any(parent => coveredSet.Contains(parent.Id))).ToArray();
        var snapshots = roots.Select(static node => node.Dehydrate()).ToArray();
        var worldSnapshots = CaptureWorldSnapshots(roots);
        return SceneCommandResults.Success(new SceneExplorerClipboard(
            this.sceneOrganizer.CloneLayout(entries)!.ToArray(),
            snapshots,
            worldSnapshots.Count == roots.Length ? worldSnapshots.ToArray() : null));
    }

    /// <inheritdoc />
    public string? ValidateExplorerPaste(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        bool cut,
        Guid? parentNodeId,
        Guid? parentFolderId,
        bool preserveWorld)
    {
        if (payload.Entries.Count == 0)
        {
            return "The clipboard is empty.";
        }

        var layout = this.CreateCompleteClipboardLayout(context.Scene);
        var destination = parentFolderId is { } folderId ? FindClipboardEntry(layout, nodeId: null, folderId)
            : parentNodeId is { } nodeId ? FindClipboardEntry(layout, nodeId, folderId: null) : null;
        if ((parentFolderId.HasValue || parentNodeId.HasValue) && destination is null)
        {
            return "The destination no longer exists.";
        }

        var scopeId = parentFolderId is { } folder ? FindFolderSceneParentNodeId(layout, folder).nodeId : parentNodeId;
        if (parentFolderId.HasValue && parentNodeId.HasValue && parentNodeId != scopeId)
        {
            return "The requested node and folder destinations belong to different scene scopes.";
        }

        var scope = scopeId is { } id ? FindNode(context.Scene, id) : null;
        if (this.FindLockedAncestor(scope) is { } locked)
        {
            return locked;
        }

        if (cut && this.ValidateCutSources(context, payload, layout, scope, destination) is { } cutFailure)
        {
            return cutFailure;
        }

        return preserveWorld ? ValidatePreservedWorld(context, payload, cut, scope) : null;
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> PasteExplorerItemsAsync(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        bool cut,
        Guid? parentNodeId,
        Guid? parentFolderId,
        bool preserveWorld,
        Guid? insertAfterNodeId = null)
    {
        using var authoring = EnterAuthoring(context);
        var reason = this.ValidateExplorerPaste(context, payload, cut, parentNodeId, parentFolderId, preserveWorld);
        if (authoring is null || reason is not null)
        {
            return this.PasteFailure("INVALID_PASTE", reason ?? "The document is reloading or was replaced.", context);
        }

        var before = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);
        var after = this.CreateCompleteClipboardLayout(context.Scene);
        var anchor = insertAfterNodeId is { } anchorId ? FindClipboardEntry(after, anchorId, folderId: null) : null;
        if (insertAfterNodeId.HasValue && anchor is null)
        {
            return this.PasteFailure("STALE_TARGET", "The sibling anchor was deleted.", context);
        }

        var scopeId = parentFolderId is { } folder ? FindFolderSceneParentNodeId(after, folder).nodeId : parentNodeId;
        var scope = scopeId is { } id ? FindNode(context.Scene, id) : null;
        var destination = parentFolderId is { } folderId ? FindClipboardEntry(after, nodeId: null, folderId)!.EnsureChildren()
            : scopeId is { } nodeId ? FindClipboardEntry(after, nodeId, folderId: null)!.EnsureChildren() : after;
        var moved = new List<ExplorerEntryData>();
        var roots = new List<SceneNode>();
        var moves = new List<ReparentMove>();
        if (cut)
        {
            StageCut(context, payload, after, scope, preserveWorld, moved, roots, moves);
        }
        else
        {
            this.StagePaste(context, payload, scope, preserveWorld, moved, roots);
        }

        if (moves.Any(static move => move.NewTransform is null))
        {
            return this.PasteFailure("TRANSFORM_UNREPRESENTABLE", "A staged hierarchy no longer has a representable destination pose.", context);
        }

        var index = anchor is null ? destination.Count : destination.IndexOf(anchor) + 1;
        foreach (var entry in moved)
        {
            destination.Insert(index++, entry);
        }

        this.ApplyPaste(context, cut, scope, insertAfterNodeId, before, after, moves, roots);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.SyncPastedItemsAsync(context, cut, scope, moves, roots).ConfigureAwait(true);
        return SceneCommandResult.Success;
    }

    private static string? ValidatePreservedWorld(SceneDocumentCommandContext context, SceneExplorerClipboard payload, bool cut, SceneNode? scope)
    {
        if (!cut && payload.WorldNodes is null && payload.Nodes.Count > 0)
        {
            return "The copied world poses cannot be represented without shear.";
        }

        foreach (var snapshot in cut ? payload.Nodes : payload.WorldNodes ?? [])
        {
            var node = cut ? FindNode(context.Scene, snapshot.Id) : SceneNode.CreateAndHydrate(context.Scene, snapshot);
            if (node is null || !SceneTransformMath.TryPreserveWorldLocal(node, scope, out _, out _, out _))
            {
                return "The destination cannot represent every captured world pose.";
            }
        }

        return null;
    }

    private static void StageCut(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        IList<ExplorerEntryData> after,
        SceneNode? scope,
        bool preserveWorld,
        List<ExplorerEntryData> moved,
        List<SceneNode> roots,
        List<ReparentMove> moves)
    {
        foreach (var entry in payload.Entries)
        {
            var actual = FindClipboardEntry(after, entry.NodeId, entry.FolderId)!;
            _ = RemoveClipboardEntry(after, actual);
            moved.Add(actual);
        }

        var movingIds = EnumerateClipboardEntries(moved).Where(static entry => entry.NodeId.HasValue)
            .Select(static entry => entry.NodeId!.Value).Distinct().ToArray();
        var movingSet = movingIds.ToHashSet();
        roots.AddRange(movingIds.Select(id => FindNode(context.Scene, id)!)
            .Where(node => !node.Ancestors().Any(parent => movingSet.Contains(parent.Id))));
        moves.AddRange(roots.Select(node => ReparentMove.Capture(node, scope, preserveWorld)));
    }

    private static void CollectSelectedEntries(IList<ExplorerEntryData> siblings, HashSet<Guid> selectedNodes, HashSet<Guid> selectedFolders, List<ExplorerEntryData> entries)
    {
        foreach (var entry in siblings)
        {
            if ((entry.NodeId is { } nodeId && selectedNodes.Contains(nodeId))
                || (entry.FolderId is { } folderId && selectedFolders.Contains(folderId)))
            {
                entries.Add(entry);
            }
            else if (entry.Children is { } children)
            {
                CollectSelectedEntries(children, selectedNodes, selectedFolders, entries);
            }
        }
    }

    private static List<SceneNodeData> CaptureWorldSnapshots(SceneNode[] roots)
    {
        var worldSnapshots = new List<SceneNodeData>();
        foreach (var node in roots)
        {
            if (!SceneTransformMath.TryPreserveWorldLocal(node, newParent: null, out var position, out var rotation, out var scale))
            {
                break;
            }

            var data = node.Dehydrate();
            worldSnapshots.Add(data with
            {
                Components = data.Components.Select(component => component is TransformData transform
                    ? transform with { Position = position, Rotation = rotation, Scale = scale } : component).ToList(),
            });
        }

        return worldSnapshots;
    }

    private static IEnumerable<ExplorerEntryData> EnumerateClipboardEntries(IEnumerable<ExplorerEntryData> entries)
    {
        foreach (var entry in entries)
        {
            yield return entry;
            if (entry.Children is { } children)
            {
                foreach (var child in EnumerateClipboardEntries(children))
                {
                    yield return child;
                }
            }
        }
    }

    private static ExplorerEntryData? FindClipboardEntry(IEnumerable<ExplorerEntryData> entries, Guid? nodeId, Guid? folderId)
        => EnumerateClipboardEntries(entries).FirstOrDefault(entry =>
            nodeId.HasValue ? entry.NodeId == nodeId : folderId.HasValue && entry.FolderId == folderId);

    private static bool RemoveClipboardEntry(IList<ExplorerEntryData> entries, ExplorerEntryData target)
    {
        if (entries.Remove(target))
        {
            return true;
        }

        return entries.Any(entry => entry.Children is { } children && RemoveClipboardEntry(children, target));
    }

    private IList<ExplorerEntryData> CreateCompleteClipboardLayout(Scene scene)
    {
        var layout = this.sceneOrganizer.CloneLayout(scene.ExplorerLayout) ?? [];
        static void EnsureNodes(IEnumerable<SceneNode> nodes, IList<ExplorerEntryData> container)
        {
            foreach (var node in nodes)
            {
                var entry = FindClipboardEntry(container, node.Id, folderId: null);
                if (entry is null)
                {
                    entry = new ExplorerEntryData { NodeId = node.Id };
                    container.Add(entry);
                }

                EnsureNodes(node.Children, entry.EnsureChildren());
            }
        }

        EnsureNodes(scene.RootNodes, layout);
        return layout;
    }

    private SceneValueCommandResult<SceneExplorerClipboard> ClipboardCaptureFailure(string code, string message, SceneDocumentCommandContext context)
        => SceneCommandResults.Failure<SceneExplorerClipboard>(this.PublishSceneFailure(
            SceneOperationKinds.NodeDuplicate,
            DiagnosticCodes.ScenePrefix + code,
            "Selection was not copied",
            message,
            context));

    private SceneCommandResult PasteFailure(string code, string message, SceneDocumentCommandContext context)
        => this.ValidationFailure(SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + code, "Items were not pasted", message, context);

    private string? FindLockedAncestor(SceneNode? start)
    {
        for (var parent = start; parent is not null; parent = parent.Parent)
        {
            if (this.interaction?.IsLocked(parent.Id) == true)
            {
                return $"Locked by {parent.Name}.";
            }
        }

        return null;
    }

    private string? ValidateCutSources(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        IList<ExplorerEntryData> layout,
        SceneNode? scope,
        ExplorerEntryData? destination)
    {
        var actualEntries = new List<ExplorerEntryData>();
        foreach (var root in payload.Entries)
        {
            if (FindClipboardEntry(layout, root.NodeId, root.FolderId) is not { } actual)
            {
                return "A staged item was deleted.";
            }

            actualEntries.Add(actual);
            if (root.FolderId is { } sourceFolderId)
            {
                var sourceScopeId = FindFolderSceneParentNodeId(layout, sourceFolderId).nodeId;
                if (this.FindLockedAncestor(sourceScopeId is { } sourceParentId ? FindNode(context.Scene, sourceParentId) : null) is { } locked)
                {
                    return locked;
                }
            }
        }

        foreach (var entry in EnumerateClipboardEntries(actualEntries))
        {
            if (this.ValidateCutEntry(context, layout, entry, scope, destination) is { } failure)
            {
                return failure;
            }
        }

        return null;
    }

    private string? ValidateCutEntry(
        SceneDocumentCommandContext context,
        IList<ExplorerEntryData> layout,
        ExplorerEntryData entry,
        SceneNode? scope,
        ExplorerEntryData? destination)
    {
        if (FindClipboardEntry(layout, entry.NodeId, entry.FolderId) is null)
        {
            return "A staged item was deleted.";
        }

        if (entry.NodeId is { } missingId && FindNode(context.Scene, missingId) is null)
        {
            return "A staged hierarchy was deleted.";
        }

        if (entry.NodeId is { } sourceId && FindNode(context.Scene, sourceId) is { } source)
        {
            if (this.interaction?.IsLocked(sourceId) == true
                || source.Ancestors().Any(parent => this.interaction?.IsLocked(parent.Id) == true))
            {
                return $"Locked by {source.Name}.";
            }

            if (ReferenceEquals(scope, source) || scope?.Ancestors().Contains(source) == true)
            {
                return "A hierarchy cannot be moved into itself or its descendants.";
            }
        }

        return destination is not null && entry.FolderId == destination.FolderId && entry.FolderId.HasValue
            ? "A folder cannot be moved into itself or its descendants."
            : null;
    }

    private void StagePaste(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        SceneNode? scope,
        bool preserveWorld,
        List<ExplorerEntryData> moved,
        List<SceneNode> roots)
    {
        var map = new Dictionary<Guid, Guid>();
        SceneNodeData Remap(SceneNodeData data)
        {
            var newId = Guid.NewGuid();
            map.Add(data.Id, newId);
            return data with
            {
                Id = newId,
                Components = data.Components.Select(RemapComponentId).ToList(),
                Children = data.Children?.Select(Remap).ToList(),
            };
        }

        ExplorerEntryData RemapEntry(ExplorerEntryData entry) => entry with
        {
            NodeId = entry.NodeId is { } node ? map[node] : null,
            FolderId = entry.FolderId.HasValue ? Guid.NewGuid() : null,
            Children = entry.Children?.Select(RemapEntry).ToList(),
        };

        foreach (var data in preserveWorld ? payload.WorldNodes ?? [] : payload.Nodes)
        {
            var snapshot = data;
            if (preserveWorld)
            {
                var original = SceneNode.CreateAndHydrate(context.Scene, snapshot);
                _ = SceneTransformMath.TryPreserveWorldLocal(original, scope, out var position, out var rotation, out var scale);
                snapshot = snapshot with
                {
                    Components = snapshot.Components.Select(component => component is TransformData transform
                        ? transform with { Position = position, Rotation = rotation, Scale = scale } : component).ToList(),
                };
            }

            var root = SceneNode.CreateAndHydrate(context.Scene, Remap(snapshot));
            this.RedirectCapturedSubtree(context, root);
            roots.Add(root);
        }

        moved.AddRange(payload.Entries.Select(RemapEntry));
    }

    private void ApplyPaste(
        SceneDocumentCommandContext context,
        bool cut,
        SceneNode? scope,
        Guid? insertAfterNodeId,
        IList<ExplorerEntryData>? before,
        IList<ExplorerEntryData> after,
        List<ReparentMove> moves,
        List<SceneNode> roots)
    {
        context.History.BeginChangeSet(cut ? "Move selected items" : "Paste selected items");
        try
        {
            var graphSiblings = scope is null ? context.Scene.RootNodes : scope.Children;
            var graphAnchor = insertAfterNodeId is { } siblingId ? FindNode(context.Scene, siblingId) : null;
            var graphInsertIndex = graphAnchor is null ? -1 : graphSiblings.IndexOf(graphAnchor) + 1;
            foreach (var move in moves)
            {
                move.NewTransform!.Value.Apply(move.Node);
                _ = this.sceneMutator.ReparentNode(move.Node.Id, move.OldParent?.Id, scope?.Id, context.Scene);
                if (graphInsertIndex >= 0)
                {
                    MoveToIndex(graphSiblings, move.Node, graphInsertIndex++);
                }
            }

            if (cut)
            {
                this.RecordReparentUndo(context, moves);
            }
            else
            {
                foreach (var node in roots)
                {
                    _ = scope is null ? this.sceneMutator.CreateNodeAtRoot(node, context.Scene)
                        : this.sceneMutator.CreateNodeUnderParent(node, scope, context.Scene);
                    if (graphInsertIndex >= 0)
                    {
                        MoveToIndex(graphSiblings, node, graphInsertIndex++);
                    }

                    this.RecordCreateNodeUndo(context, node);
                }
            }

            context.Scene.SetExplorerLayout(after);
            this.RecordLayoutHistory(context, "Paste layout", before, after);
        }
        finally
        {
            context.History.EndChangeSet();
        }
    }

    private async Task SyncPastedItemsAsync(SceneDocumentCommandContext context, bool cut, SceneNode? scope, List<ReparentMove> moves, List<SceneNode> roots)
    {
        if (!cut)
        {
            foreach (var node in roots)
            {
                await this.SyncNodeSubtreeAsync(context, node).ConfigureAwait(true);
                this.PublishNodeAdded(context, node);
            }

            return;
        }

        foreach (var move in moves)
        {
            var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
            _ = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeReparent, outcome).ConfigureAwait(true);
        }

        if (moves.Count > 0)
        {
            await this.sceneEngineSync.ReparentHierarchiesAsync(
                context.Scene,
                moves.Select(static move => move.Node.Id).ToArray(),
                scope?.Id,
                preserveWorldTransform: false).ConfigureAwait(true);
        }
    }
}
