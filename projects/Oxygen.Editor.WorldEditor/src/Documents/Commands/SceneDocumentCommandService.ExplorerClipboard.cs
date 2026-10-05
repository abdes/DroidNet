// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
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
        var selectedNodes = nodeIds.ToHashSet();
        var selectedFolders = folderIds.ToHashSet();
        if (authoring is null || nodeIds.Any(id => FindNode(context.Scene, id) is null)
            || folderIds.Any(id => FindClipboardEntry(layout, null, id) is null))
        {
            return SceneCommandResults.Failure<SceneExplorerClipboard>(this.PublishSceneFailure(
                SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "Selection was not copied", "A selected item no longer belongs to the loaded scene.", context));
        }

        var entries = new List<ExplorerEntryData>();
        void Collect(IList<ExplorerEntryData> siblings)
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
                    Collect(children);
                }
            }
        }

        Collect(layout);
        if (entries.Count == 0)
        {
            return SceneCommandResults.Failure<SceneExplorerClipboard>(this.PublishSceneFailure(
                SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "EMPTY_SELECTION",
                "Selection was not copied", "Select scene nodes or folders to copy.", context));
        }

        var coveredIds = EnumerateClipboardEntries(entries).Where(static entry => entry.NodeId.HasValue)
            .Select(static entry => entry.NodeId!.Value).Distinct().ToArray();
        if (coveredIds.Any(id => FindNode(context.Scene, id) is null))
        {
            return SceneCommandResults.Failure<SceneExplorerClipboard>(this.PublishSceneFailure(
                SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "Selection was not copied", "A selected folder references a node that no longer exists.", context));
        }

        var coveredSet = coveredIds.ToHashSet();
        var roots = coveredIds.Select(id => FindNode(context.Scene, id)!).Where(node =>
            !node.Ancestors().Any(parent => coveredSet.Contains(parent.Id))).ToArray();
        var snapshots = roots.Select(static node => node.Dehydrate()).ToArray();
        var worldSnapshots = new List<SceneNodeData>();
        foreach (var node in roots)
        {
            if (!SceneTransformMath.TryPreserveWorldLocal(node, null, out var position, out var rotation, out var scale))
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

        return SceneCommandResults.Success(new SceneExplorerClipboard(
            this.sceneOrganizer.CloneLayout(entries)!.ToArray(), snapshots,
            worldSnapshots.Count == roots.Length ? worldSnapshots.ToArray() : null));
    }

    /// <inheritdoc />
    public string? ValidateExplorerPaste(SceneDocumentCommandContext context, SceneExplorerClipboard payload,
        bool cut, Guid? parentNodeId, Guid? parentFolderId, bool preserveWorld)
    {
        if (payload.Entries.Count == 0)
        {
            return "The clipboard is empty.";
        }

        var layout = this.CreateCompleteClipboardLayout(context.Scene);
        var destination = parentFolderId is { } folderId ? FindClipboardEntry(layout, null, folderId)
            : parentNodeId is { } nodeId ? FindClipboardEntry(layout, nodeId, null) : null;
        if ((parentFolderId.HasValue || parentNodeId.HasValue) && destination is null)
        {
            return "The destination no longer exists.";
        }

        var scopeId = parentFolderId is { } folder ? FindFolderSceneParentNodeId(layout, folder).NodeId : parentNodeId;
        if (parentFolderId.HasValue && parentNodeId.HasValue && parentNodeId != scopeId)
        {
            return "The requested node and folder destinations belong to different scene scopes.";
        }

        var scope = scopeId is { } id ? FindNode(context.Scene, id) : null;
        for (var parent = scope; parent is not null; parent = parent.Parent)
        {
            if (this.interaction?.IsLocked(parent.Id) == true)
            {
                return $"Locked by {parent.Name}.";
            }
        }

        if (cut)
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
                    var sourceScopeId = FindFolderSceneParentNodeId(layout, sourceFolderId).NodeId;
                    for (var parent = sourceScopeId is { } sourceParentId ? FindNode(context.Scene, sourceParentId) : null;
                        parent is not null; parent = parent.Parent)
                    {
                        if (this.interaction?.IsLocked(parent.Id) == true)
                        {
                            return $"Locked by {parent.Name}.";
                        }
                    }
                }
            }

            foreach (var entry in EnumerateClipboardEntries(actualEntries))
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

                if (destination is not null && entry.FolderId == destination.FolderId && entry.FolderId.HasValue)
                {
                    return "A folder cannot be moved into itself or its descendants.";
                }
            }
        }

        if (preserveWorld)
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
        }

        return null;
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> PasteExplorerItemsAsync(SceneDocumentCommandContext context,
        SceneExplorerClipboard payload, bool cut, Guid? parentNodeId, Guid? parentFolderId,
        bool preserveWorld, Guid? insertAfterNodeId = null)
    {
        using var authoring = EnterAuthoring(context);
        var reason = this.ValidateExplorerPaste(context, payload, cut, parentNodeId, parentFolderId, preserveWorld);
        if (authoring is null || reason is not null)
        {
            return this.ValidationFailure(SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "INVALID_PASTE",
                "Items were not pasted", reason ?? "The document is reloading or was replaced.", context);
        }

        var before = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);
        var after = this.CreateCompleteClipboardLayout(context.Scene);
        var anchor = insertAfterNodeId is { } anchorId ? FindClipboardEntry(after, anchorId, null) : null;
        if (insertAfterNodeId.HasValue && anchor is null)
        {
            return this.ValidationFailure(SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "Items were not pasted", "The sibling anchor was deleted.", context);
        }

        var scopeId = parentFolderId is { } folder ? FindFolderSceneParentNodeId(after, folder).NodeId : parentNodeId;
        var scope = scopeId is { } id ? FindNode(context.Scene, id) : null;
        var destination = parentFolderId is { } folderId ? FindClipboardEntry(after, null, folderId)!.EnsureChildren()
            : scopeId is { } nodeId ? FindClipboardEntry(after, nodeId, null)!.EnsureChildren() : after;
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

        var moved = new List<ExplorerEntryData>();
        var roots = new List<SceneNode>();
        var moves = new List<ReparentMove>();
        if (cut)
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
        else
        {
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

                roots.Add(SceneNode.CreateAndHydrate(context.Scene, Remap(snapshot)));
            }

            moved.AddRange(payload.Entries.Select(RemapEntry));
        }

        if (moves.Any(static move => move.NewTransform is null))
        {
            return this.ValidationFailure(SceneOperationKinds.NodeDuplicate, DiagnosticCodes.ScenePrefix + "TRANSFORM_UNREPRESENTABLE",
                "Items were not pasted", "A staged hierarchy no longer has a representable destination pose.", context);
        }

        var index = anchor is null ? destination.Count : destination.IndexOf(anchor) + 1;
        foreach (var entry in moved)
        {
            destination.Insert(index++, entry);
        }

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

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        if (cut)
        {
            foreach (var move in moves)
            {
                var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
                _ = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeReparent, outcome).ConfigureAwait(true);
            }

            if (moves.Count > 0)
            {
                await this.sceneEngineSync.ReparentHierarchiesAsync(context.Scene,
                    moves.Select(static move => move.Node.Id).ToArray(), scope?.Id, preserveWorldTransform: false).ConfigureAwait(true);
            }
        }
        else
        {
            foreach (var node in roots)
            {
                await this.SyncNodeSubtreeAsync(context, node).ConfigureAwait(true);
                this.PublishNodeAdded(context, node);
            }
        }

        return SceneCommandResult.Success;
    }

    private IList<ExplorerEntryData> CreateCompleteClipboardLayout(Scene scene)
    {
        var layout = this.sceneOrganizer.CloneLayout(scene.ExplorerLayout) ?? [];
        void EnsureNodes(IEnumerable<SceneNode> nodes, IList<ExplorerEntryData> container)
        {
            foreach (var node in nodes)
            {
                var entry = FindClipboardEntry(container, node.Id, null);
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
}
