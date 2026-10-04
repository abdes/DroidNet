// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using DroidNet.TimeMachine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Identity-based hierarchy authoring commands for <see cref="SceneDocumentCommandService"/>.
/// </summary>
/// <remarks>
/// These commands are the single authority for explorer hierarchy mutation: each validates the
/// complete request, commits graph and layout together, advances history and dirty state
/// synchronously, then requests native convergence. Folders are grouping-only and never change
/// scene parenting or transforms.
/// </remarks>
public sealed partial class SceneDocumentCommandService
{
    private static readonly StringComparer LayoutTypeComparer = StringComparer.OrdinalIgnoreCase;

    /// <inheritdoc />
    public async Task<SceneValueCommandResult<SceneNode>> CreateNodeAsync(
        SceneDocumentCommandContext context,
        Guid? parentNodeId,
        Guid? parentFolderId,
        string name)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return SceneCommandResults.Failure<SceneNode>();
        }

        var trimmed = (name ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return SceneCommandResults.Failure<SceneNode>(this.PublishSceneFailure(
                SceneOperationKinds.NodeCreate,
                DiagnosticCodes.ScenePrefix + "INVALID_NAME",
                "Node was not created",
                "Scene node names cannot be empty.",
                context));
        }

        SceneNode? parentNode = null;
        if (parentNodeId.HasValue)
        {
            parentNode = FindNode(context.Scene, parentNodeId.Value);
            if (parentNode is null)
            {
                return SceneCommandResults.Failure<SceneNode>(this.PublishSceneFailure(
                    SceneOperationKinds.NodeCreate,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Node was not created",
                    "The target parent node no longer exists.",
                    context));
            }
        }

        Guid? folderSceneParentNodeId = null;
        if (parentFolderId.HasValue)
        {
            var (found, nodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, parentFolderId.Value);
            if (!found)
            {
                return SceneCommandResults.Failure<SceneNode>(this.PublishSceneFailure(
                    SceneOperationKinds.NodeCreate,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Node was not created",
                    "The target folder no longer exists.",
                    context));
            }

            folderSceneParentNodeId = nodeId;
        }

        var node = new SceneNode(context.Scene) { Name = trimmed };
        SceneNode? actualParent = parentNode ?? (folderSceneParentNodeId.HasValue ? FindNode(context.Scene, folderSceneParentNodeId.Value) : null);
        _ = actualParent is null
            ? this.sceneMutator.CreateNodeAtRoot(node, context.Scene)
            : this.sceneMutator.CreateNodeUnderParent(node, actualParent, context.Scene);

        if (parentFolderId.HasValue)
        {
            EnsureExplorerLayout(context.Scene);
            _ = this.sceneOrganizer.MoveNodeToFolder(node.Id, parentFolderId.Value, context.Scene);
        }

        this.RecordCreateNodeUndo(context, node);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.SyncCreateNodeAsync(context, node, actualParent?.Id).ConfigureAwait(true);
        this.PublishNodeAdded(context, node);
        return SceneCommandResults.Success(node);
    }

    /// <inheritdoc />
    public Task<SceneValueCommandResult<Guid>> CreateFolderAsync(
        SceneDocumentCommandContext context,
        Guid? parentFolderId,
        Guid? parentNodeId,
        string name)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return Task.FromResult(SceneCommandResults.Failure<Guid>());
        }

        var trimmed = (name ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return Task.FromResult(SceneCommandResults.Failure<Guid>(this.PublishSceneFailure(
                SceneOperationKinds.ExplorerFolderCreate,
                DiagnosticCodes.ScenePrefix + "INVALID_NAME",
                "Folder was not created",
                "Folder names cannot be empty.",
                context)));
        }

        // Validate the parent folder before seeding: the organizer resolves it against the layout and
        // would throw after the seed, leaving an uncommitted, un-undoable mutation behind a generic failure.
        if (parentFolderId.HasValue)
        {
            var (folderFound, _) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, parentFolderId.Value);
            if (!folderFound)
            {
                return Task.FromResult(SceneCommandResults.Failure<Guid>(this.PublishSceneFailure(
                    SceneOperationKinds.ExplorerFolderCreate,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Folder was not created",
                    "The target parent folder no longer exists.",
                    context)));
            }
        }

        // Validate the parent node before seeding for the same reason: the organizer ensures the node's
        // layout entry and throws for a node missing from the scene graph only after the seed happened.
        if (parentNodeId.HasValue && FindNode(context.Scene, parentNodeId.Value) is null)
        {
            return Task.FromResult(SceneCommandResults.Failure<Guid>(this.PublishSceneFailure(
                SceneOperationKinds.ExplorerFolderCreate,
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "Folder was not created",
                "The target parent node no longer exists.",
                context)));
        }

        try
        {
            EnsureExplorerLayout(context.Scene);
            var change = this.sceneOrganizer.CreateFolder(parentFolderId, trimmed, context.Scene, parentNodeId: parentNodeId);
            var folderId = change.NewFolder?.FolderId ?? Guid.Empty;
            if (folderId == Guid.Empty)
            {
                return Task.FromResult(SceneCommandResults.Failure<Guid>());
            }

            this.RecordLayoutHistory(context, $"Create folder '{trimmed}'", change.PreviousLayout, change.NewLayout);
            _ = this.MarkDirtyAsync(context);
            return Task.FromResult(SceneCommandResults.Success(folderId));
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerFolderCreate,
                DiagnosticCodes.ScenePrefix + "CREATE_FOLDER_FAILED",
                "Folder was not created",
                $"The folder '{trimmed}' could not be created.",
                context,
                ex);
            return Task.FromResult(SceneCommandResults.Failure<Guid>(operationResultId));
        }
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> RenameNodeAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        string newName)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        var node = FindNode(context.Scene, nodeId);
        if (node is null)
        {
            return this.ValidationFailure(
                SceneOperationKinds.NodeRename,
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "Node was not renamed",
                "The scene node no longer exists.",
                context);
        }

        var trimmed = (newName ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return this.ValidationFailure(
                SceneOperationKinds.NodeRename,
                DiagnosticCodes.ScenePrefix + "INVALID_NAME",
                "Node was not renamed",
                "Scene node names cannot be empty.",
                context);
        }

        var oldName = node.Name;
        if (string.Equals(oldName, trimmed, StringComparison.Ordinal))
        {
            return SceneCommandResult.Success;
        }

        node.Name = trimmed;
        context.History.AddChange(
            $"Rename({oldName} -> {trimmed})",
            async () => await this.RenameNodeAsync(context, nodeId, oldName).ConfigureAwait(false));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var outcome = await this.sceneEngineSync.RenameNodeAsync(context.Scene, nodeId, trimmed).ConfigureAwait(true);
        var operationResultId = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeRename, outcome).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    /// <inheritdoc />
    public Task<SceneCommandResult> RenameFolderAsync(
        SceneDocumentCommandContext context,
        Guid folderId,
        string newName)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return Task.FromResult(new SceneCommandResult(Succeeded: false));
        }

        var trimmed = (newName ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return Task.FromResult(this.ValidationFailure(
                SceneOperationKinds.ExplorerFolderRename,
                DiagnosticCodes.ScenePrefix + "INVALID_NAME",
                "Folder was not renamed",
                "Folder names cannot be empty.",
                context));
        }

        try
        {
            EnsureExplorerLayout(context.Scene);
            var change = this.sceneOrganizer.RenameFolder(folderId, trimmed, context.Scene);
            this.RecordLayoutHistory(context, $"Rename folder to '{trimmed}'", change.PreviousLayout, change.NewLayout);
            _ = this.MarkDirtyAsync(context);
            return Task.FromResult(SceneCommandResult.Success);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerFolderRename,
                DiagnosticCodes.ScenePrefix + "RENAME_FAILED",
                "Folder was not renamed",
                "The folder could not be renamed.",
                context,
                ex);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, operationResultId));
        }
    }

    /// <inheritdoc />
    public Task<SceneCommandResult> DeleteNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds)
        => this.DeleteItemsAsync(context, nodeIds, []);

    /// <inheritdoc />
    public Task<SceneCommandResult> DeleteFolderAsync(
        SceneDocumentCommandContext context,
        Guid folderId)
        => this.DeleteItemsAsync(context, [], [folderId]);

    /// <inheritdoc />
    public async Task<SceneCommandResult> DeleteItemsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        IReadOnlyList<Guid> folderIds)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        var topLevelIds = this.sceneOrganizer.FilterTopLevelSelectedNodeIds([.. nodeIds], context.Scene);

        // Pre-validate nodes before any mutation.
        var restores = new List<NodeRestore>(topLevelIds.Count);
        foreach (var nodeId in topLevelIds)
        {
            var node = FindNode(context.Scene, nodeId);
            if (node is null)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeDelete,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Items were not deleted",
                    "One or more selected nodes no longer exist.",
                    context);
            }

            restores.Add(NodeRestore.Capture(node));
        }

        // Pre-validate folders before any mutation.
        foreach (var folderId in folderIds)
        {
            var (found, _) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, folderId);
            if (!found)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.ExplorerFolderDelete,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Items were not deleted",
                    "One or more selected folders no longer exist.",
                    context);
            }
        }

        // Seed the layout only after every validation passed, so a rejected delete leaves no mutation behind.
        EnsureExplorerLayout(context.Scene);
        var layoutBefore = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);

        context.History.BeginChangeSet($"Delete {restores.Count + folderIds.Count} item(s)");
        try
        {
            foreach (var restore in restores)
            {
                _ = this.sceneMutator.RemoveHierarchy(restore.Node.Id, context.Scene);
                _ = this.sceneOrganizer.RemoveNodeFromLayout(restore.Node.Id, context.Scene);
            }

            this.RecordDeleteNodesUndo(context, restores);

            foreach (var folderId in folderIds)
            {
                _ = this.sceneOrganizer.RemoveFolder(folderId, promoteChildrenToParent: true, context.Scene);
            }

            this.RecordLayoutHistory(context, "Delete folders", layoutBefore, context.Scene.ExplorerLayout);
        }
        finally
        {
            context.History.EndChangeSet();
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.SyncRemoveNodesAsync(context, [.. topLevelIds]).ConfigureAwait(true);

        foreach (var restore in restores)
        {
            this.PublishNodeRemoved(context, restore.Node);
        }

        return SceneCommandResult.Success;
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> ReparentNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid? newParentNodeId,
        bool preserveWorldTransform,
        Guid? insertAfterNodeId = null)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        var topLevelIds = this.sceneOrganizer.FilterTopLevelSelectedNodeIds([.. nodeIds], context.Scene);
        SceneNode? newParent = null;
        if (newParentNodeId.HasValue)
        {
            newParent = FindNode(context.Scene, newParentNodeId.Value);
            if (newParent is null)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Nodes were not moved",
                    "The destination parent no longer exists.",
                    context);
            }
        }

        // Inserting after a sibling implies the sibling's parent.
        var insertIndex = -1;
        if (insertAfterNodeId.HasValue)
        {
            var anchor = FindNode(context.Scene, insertAfterNodeId.Value);
            if (anchor is null)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Nodes were not moved",
                    "The destination sibling no longer exists.",
                    context);
            }

            newParent = anchor.Parent;
            insertIndex = anchor.Parent is null
                ? context.Scene.RootNodes.IndexOf(anchor) + 1
                : anchor.Parent.Children.IndexOf(anchor) + 1;
        }

        var moves = new List<ReparentMove>(topLevelIds.Count);
        foreach (var nodeId in topLevelIds)
        {
            var node = FindNode(context.Scene, nodeId);
            if (node is null)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Nodes were not moved",
                    "One or more selected nodes no longer exist.",
                    context);
            }

            if (ReferenceEquals(node, newParent) || (newParent is not null && newParent.Ancestors().Contains(node)))
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.ScenePrefix + "INVALID_CYCLE",
                    "Nodes were not moved",
                    "A node cannot be moved into its own descendant.",
                    context);
            }

            var move = ReparentMove.Capture(node, newParent, preserveWorldTransform);
            if (move.NewTransform is null)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.ScenePrefix + "TRANSFORM_UNREPRESENTABLE",
                    "Nodes were not moved",
                    "The destination would produce an unrepresentable transform (singular or sheared).",
                    context);
            }

            moves.Add(move);
        }

        EnsureExplorerLayout(context.Scene);
        foreach (var move in moves)
        {
            move.ApplyForward(this.sceneMutator, this.sceneOrganizer);
            if (insertIndex >= 0)
            {
                MoveToIndex(newParent is null ? context.Scene.RootNodes : newParent.Children, move.Node, insertIndex);
                insertIndex++;
            }
        }

        this.RecordReparentUndo(context, moves);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        // Publish the compensated local TRS before reparenting native with preserve-world
        // disabled, so the native side applies the new local TRS instead of a stale one.
        Guid? firstOperationResultId = null;
        foreach (var move in moves)
        {
            var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
            firstOperationResultId ??= await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeReparent, outcome).ConfigureAwait(true);
        }

        await this.sceneEngineSync.ReparentHierarchiesAsync(context.Scene, [.. topLevelIds], newParentNodeId, preserveWorldTransform: false).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, firstOperationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> MoveNodesToFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid folderId)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        try
        {
            EnsureExplorerLayout(context.Scene);

            // Resolve the folder's scene-parent scope (the enclosing node, or root when null).
            var (folderFound, folderSceneParentNodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, folderId);
            if (!folderFound)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.ExplorerLayoutMoveNode,
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "Nodes were not grouped",
                    "The target folder no longer exists.",
                    context);
            }

            var folderScopeParent = folderSceneParentNodeId.HasValue
                ? FindNode(context.Scene, folderSceneParentNodeId.Value)
                : null;

            // Pre-resolve every node and the reparent it needs (preserve-local), before mutating.
            var nodes = new List<SceneNode>(nodeIds.Count);
            var reparents = new List<ReparentMove>();
            foreach (var nodeId in nodeIds)
            {
                var node = FindNode(context.Scene, nodeId);
                if (node is null)
                {
                    return this.ValidationFailure(
                        SceneOperationKinds.ExplorerLayoutMoveNode,
                        DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                        "Nodes were not grouped",
                        "One or more selected nodes no longer exist.",
                        context);
                }

                nodes.Add(node);

                if (node.Parent?.Id == folderSceneParentNodeId)
                {
                    continue;
                }

                if (folderScopeParent is not null
                    && (ReferenceEquals(node, folderScopeParent) || folderScopeParent.Ancestors().Contains(node)))
                {
                    return this.ValidationFailure(
                        SceneOperationKinds.ExplorerLayoutMoveNode,
                        DiagnosticCodes.ScenePrefix + "INVALID_CYCLE",
                        "Nodes were not grouped",
                        "A node cannot be grouped under its own descendant.",
                        context);
                }

                reparents.Add(ReparentMove.Capture(node, folderScopeParent, preserveWorldTransform: false));
            }

            var previousLayout = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);

            context.History.BeginChangeSet($"Group {nodes.Count} node(s)");
            try
            {
                foreach (var move in reparents)
                {
                    move.ApplyForward(this.sceneMutator, this.sceneOrganizer);
                }

                this.RecordReparentUndo(context, reparents);

                foreach (var nodeId in nodeIds)
                {
                    _ = this.sceneOrganizer.MoveNodeToFolder(nodeId, folderId, context.Scene);
                }

                this.RecordLayoutHistory(context, $"Group {nodes.Count} node(s)", previousLayout, context.Scene.ExplorerLayout);
            }
            finally
            {
                context.History.EndChangeSet();
            }

            _ = this.MarkDirtyAsync(context);

            // Push each cross-scope reparent's local TRS and surface a native rejection instead of
            // discarding it, so the grouping never diverges in the runtime silently.
            Guid? firstOperationResultId = null;
            foreach (var move in reparents)
            {
                var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
                firstOperationResultId ??= await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.ExplorerLayoutMoveNode, outcome).ConfigureAwait(true);
                _ = this.sceneEngineSync.ReparentNodeAsync(context.Scene, move.Node.Id, move.NewParent?.Id, preserveWorldTransform: false);
            }

            return new SceneCommandResult(Succeeded: true, firstOperationResultId);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerLayoutMoveNode,
                DiagnosticCodes.ScenePrefix + "MOVE_TO_FOLDER_FAILED",
                "Nodes were not grouped",
                ex.Message,
                context,
                ex);
            return new SceneCommandResult(Succeeded: false, operationResultId);
        }
    }

    /// <inheritdoc />
    public Task<SceneCommandResult> RemoveNodesFromFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid folderId)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return Task.FromResult(new SceneCommandResult(Succeeded: false));
        }

        try
        {
            EnsureExplorerLayout(context.Scene);
            var previousLayout = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);
            foreach (var nodeId in nodeIds)
            {
                _ = this.sceneOrganizer.RemoveNodeFromFolder(nodeId, folderId, context.Scene);
            }

            this.RecordLayoutHistory(context, "Remove from folder", previousLayout, context.Scene.ExplorerLayout);
            _ = this.MarkDirtyAsync(context);
            return Task.FromResult(SceneCommandResult.Success);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerLayoutMoveNode,
                DiagnosticCodes.ScenePrefix + "REMOVE_FROM_FOLDER_FAILED",
                "Nodes were not removed from the folder",
                ex.Message,
                context,
                ex);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, operationResultId));
        }
    }

    /// <inheritdoc />
    public Task<SceneCommandResult> MoveFolderToParentAsync(
        SceneDocumentCommandContext context,
        Guid folderId,
        Guid? newParentFolderId)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return Task.FromResult(new SceneCommandResult(Succeeded: false));
        }

        try
        {
            EnsureExplorerLayout(context.Scene);
            var previousLayout = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);
            _ = this.sceneOrganizer.MoveFolderToParent(folderId, newParentFolderId, context.Scene);
            this.RecordLayoutHistory(context, "Move folder", previousLayout, context.Scene.ExplorerLayout);
            _ = this.MarkDirtyAsync(context);
            return Task.FromResult(SceneCommandResult.Success);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerLayoutMoveNode,
                DiagnosticCodes.ScenePrefix + "MOVE_FOLDER_FAILED",
                "Folder was not moved",
                ex.Message,
                context,
                ex);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, operationResultId));
        }
    }

    /// <inheritdoc />
    public Task<SceneCommandResult> ReorderNodesAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid? parentFolderId,
        Guid? parentNodeId,
        int index)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return Task.FromResult(new SceneCommandResult(Succeeded: false));
        }

        try
        {
            EnsureExplorerLayout(context.Scene);
            var previousLayout = this.sceneOrganizer.CloneLayout(context.Scene.ExplorerLayout);
            _ = this.sceneOrganizer.MoveNodeToSiblingIndex(nodeId, parentFolderId, parentNodeId, index, context.Scene);
            this.RecordLayoutHistory(context, "Reorder", previousLayout, context.Scene.ExplorerLayout);
            _ = this.MarkDirtyAsync(context);
            return Task.FromResult(SceneCommandResult.Success);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.ExplorerLayoutMoveNode,
                DiagnosticCodes.ScenePrefix + "REORDER_FAILED",
                "Node was not reordered",
                ex.Message,
                context,
                ex);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, operationResultId));
        }
    }

    private void RecordCreateNodeUndo(SceneDocumentCommandContext context, SceneNode node)
    {
        var restore = NodeRestore.Capture(node);
        context.History.AddChange(
            $"Remove {node.Name}",
            async () => await this.RemoveNodeForUndoAsync(context, restore).ConfigureAwait(true));
    }

    private async Task RemoveNodeForUndoAsync(SceneDocumentCommandContext context, NodeRestore restore)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        EnsureExplorerLayout(context.Scene);
        _ = this.sceneMutator.RemoveHierarchy(restore.Node.Id, context.Scene);
        _ = this.sceneOrganizer.RemoveNodeFromLayout(restore.Node.Id, context.Scene);
        this.RecordDeleteNodesUndo(context, [restore]);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.SyncRemoveNodesAsync(context, [restore.Node.Id]).ConfigureAwait(true);
        this.PublishNodeRemoved(context, restore.Node);
    }

    private void RecordDeleteNodesUndo(SceneDocumentCommandContext context, IReadOnlyList<NodeRestore> restores)
    {
        context.History.BeginChangeSet($"Restore {restores.Count} node(s)");
        try
        {
            foreach (var restore in restores)
            {
                var name = restore.Node.Name;
                context.History.AddChange(
                    $"Restore {name}",
                    async () => await this.RestoreNodeForUndoAsync(context, restore).ConfigureAwait(true));
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }
    }

    private async Task RestoreNodeForUndoAsync(SceneDocumentCommandContext context, NodeRestore restore)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        InsertNodeAt(context.Scene, restore.Node, restore.Parent, restore.Index);
        this.RecordCreateNodeUndo(context, restore.Node);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.SyncNodeSubtreeAsync(context, restore.Node).ConfigureAwait(true);
        this.PublishNodeAdded(context, restore.Node);
    }

    private void RecordReparentUndo(SceneDocumentCommandContext context, IReadOnlyList<ReparentMove> moves)
    {
        context.History.BeginChangeSet($"Move {moves.Count} node(s)");
        try
        {
            foreach (var move in moves)
            {
                context.History.AddChange(
                    $"Move {move.Node.Name}",
                    async () => await this.UndoReparentAsync(context, move).ConfigureAwait(true));
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }
    }

    private async Task UndoReparentAsync(SceneDocumentCommandContext context, ReparentMove move)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        move.ApplyInverse(this.sceneMutator, this.sceneOrganizer);
        context.History.AddChange(
            $"Move {move.Node.Name}",
            async () => await this.RedoReparentAsync(context, move).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        // Surface a native rejection from inside the history delegate: operation results reach the
        // user through the operations channel regardless of who initiated the reparent.
        var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
        _ = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeReparent, outcome).ConfigureAwait(true);
        await this.sceneEngineSync.ReparentNodeAsync(context.Scene, move.Node.Id, move.OldParent?.Id, preserveWorldTransform: false).ConfigureAwait(true);
    }

    private async Task RedoReparentAsync(SceneDocumentCommandContext context, ReparentMove move)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        move.ApplyForward(this.sceneMutator, this.sceneOrganizer);
        context.History.AddChange(
            $"Move {move.Node.Name}",
            async () => await this.UndoReparentAsync(context, move).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        // Surface a native rejection from inside the history delegate, as in UndoReparentAsync.
        var outcome = await this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, move.Node).ConfigureAwait(true);
        _ = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.NodeReparent, outcome).ConfigureAwait(true);
        await this.sceneEngineSync.ReparentNodeAsync(context.Scene, move.Node.Id, move.NewParent?.Id, preserveWorldTransform: false).ConfigureAwait(true);
    }

    private void RecordLayoutHistory(
        SceneDocumentCommandContext context,
        string label,
        IList<ExplorerEntryData>? before,
        IList<ExplorerEntryData>? after)
    {
        var beforeClone = this.sceneOrganizer.CloneLayout(before);
        var afterClone = this.sceneOrganizer.CloneLayout(after);
        if (LayoutsEqual(beforeClone, afterClone))
        {
            return;
        }

        context.History.AddChange(
            label,
            async () => await this.ApplyLayoutForHistoryAsync(context, label, beforeClone, afterClone).ConfigureAwait(true));
    }

    private async Task ApplyLayoutForHistoryAsync(
        SceneDocumentCommandContext context,
        string label,
        IList<ExplorerEntryData>? layout,
        IList<ExplorerEntryData>? inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        context.Scene.SetExplorerLayout(this.sceneOrganizer.CloneLayout(layout));
        context.History.AddChange(
            label,
            async () => await this.ApplyLayoutForHistoryAsync(context, label, inverse, layout).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private async Task SyncCreateNodeAsync(SceneDocumentCommandContext context, SceneNode node, Guid? parentId)
    {
        try
        {
            await this.sceneEngineSync.CreateNodeAsync(node, parentId).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            _ = this.PublishLiveSyncWarning(SceneOperationKinds.NodeCreate, DiagnosticCodes.LiveSyncPrefix + "CREATE_NODE_FAILED", "Scene was updated but live preview was not", context, node, ex);
        }
    }

    private async Task SyncRemoveNodesAsync(SceneDocumentCommandContext context, IReadOnlyList<Guid> nodeIds)
    {
        try
        {
            await this.sceneEngineSync.RemoveNodeHierarchiesAsync(context.Scene, nodeIds).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            _ = this.PublishSceneWarning(
                SceneOperationKinds.NodeDelete,
                DiagnosticCodes.LiveSyncPrefix + "REMOVE_NODE_FAILED",
                "Scene was updated but live preview was not",
                "Deleted nodes were authored, but the live preview did not update.",
                context,
                ex,
                FailureDomain.LiveSync);
        }
    }

    private static void EnsureExplorerLayout(Scene scene)
    {
        if (scene.ExplorerLayout is not null)
        {
            return;
        }

        var layout = new List<ExplorerEntryData>(scene.RootNodes.Count);
        foreach (var root in scene.RootNodes)
        {
            layout.Add(new ExplorerEntryData { Type = "Node", NodeId = root.Id });
        }

        scene.SetExplorerLayout(layout);
    }

    private static void InsertNodeAt(Scene scene, SceneNode node, SceneNode? parent, int index)
    {
        if (parent is null)
        {
            node.SetParent(null);
            if (!scene.RootNodes.Contains(node))
            {
                scene.RootNodes.Add(node);
            }

            MoveToIndex(scene.RootNodes, node, index);
        }
        else
        {
            node.SetParent(parent);
            MoveToIndex(parent.Children, node, index);
        }
    }

    private static void MoveToIndex(System.Collections.ObjectModel.ObservableCollection<SceneNode> collection, SceneNode node, int index)
    {
        var target = Math.Clamp(index, 0, Math.Max(0, collection.Count - 1));
        var current = collection.IndexOf(node);
        if (current >= 0 && current != target)
        {
            collection.Move(current, target);
        }
    }

    private static (bool Found, Guid? NodeId) FindFolderSceneParentNodeId(IList<ExplorerEntryData>? entries, Guid folderId, Guid? enclosingNodeId = null)
    {
        if (entries is null)
        {
            return (false, null);
        }

        foreach (var entry in entries)
        {
            var current = enclosingNodeId;
            if (LayoutTypeComparer.Equals(entry.Type, "Node") && entry.NodeId.HasValue)
            {
                current = entry.NodeId;
            }

            if (LayoutTypeComparer.Equals(entry.Type, "Folder") && entry.FolderId == folderId)
            {
                return (true, current);
            }

            if (entry.Children is not null)
            {
                var result = FindFolderSceneParentNodeId(entry.Children, folderId, current);
                if (result.Found)
                {
                    return result;
                }
            }
        }

        return (false, null);
    }

    private static bool LayoutsEqual(IList<ExplorerEntryData>? left, IList<ExplorerEntryData>? right)
    {
        if (left is null || right is null)
        {
            return left is null && right is null;
        }

        if (left.Count != right.Count)
        {
            return false;
        }

        for (var i = 0; i < left.Count; i++)
        {
            if (!EntryEqual(left[i], right[i]))
            {
                return false;
            }
        }

        return true;
    }

    private static bool EntryEqual(ExplorerEntryData left, ExplorerEntryData right)
        => string.Equals(left.Type, right.Type, StringComparison.OrdinalIgnoreCase)
           && left.NodeId == right.NodeId
           && left.FolderId == right.FolderId
           && string.Equals(left.Name, right.Name, StringComparison.Ordinal)
           && left.IsExpanded == right.IsExpanded
           && LayoutsEqual(left.Children, right.Children);

    private sealed record NodeRestore(SceneNode Node, SceneNode? Parent, int Index)
    {
        public static NodeRestore Capture(SceneNode node)
        {
            var index = node.Parent is null
                ? node.Scene.RootNodes.IndexOf(node)
                : node.Parent.Children.IndexOf(node);
            return new NodeRestore(node, node.Parent, index);
        }
    }

    private sealed record ReparentMove(
        SceneNode Node,
        SceneNode? OldParent,
        int OldIndex,
        SceneNode? NewParent,
        TransformSnapshot OldTransform,
        TransformSnapshot? NewTransform)
    {
        public static ReparentMove Capture(SceneNode node, SceneNode? newParent, bool preserveWorldTransform)
        {
            var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
            var oldTransform = TransformSnapshot.Capture(transform);
            var oldIndex = node.Parent is null
                ? node.Scene.RootNodes.IndexOf(node)
                : node.Parent.Children.IndexOf(node);

            TransformSnapshot? newTransform = null;
            if (preserveWorldTransform && transform is not null
                && SceneTransformMath.TryPreserveWorldLocal(node, newParent, out var position, out var rotation, out var scale))
            {
                newTransform = new TransformSnapshot(position, rotation, scale);
            }
            else if (!preserveWorldTransform)
            {
                newTransform = oldTransform;
            }

            return new ReparentMove(node, node.Parent, oldIndex, newParent, oldTransform, newTransform);
        }

        public void ApplyForward(ISceneMutator mutator, ISceneOrganizer organizer)
        {
            this.NewTransform?.Apply(this.Node);
            _ = mutator.ReparentNode(this.Node.Id, this.OldParent?.Id, this.NewParent?.Id, this.Node.Scene);
            _ = organizer.RemoveNodeFromLayout(this.Node.Id, this.Node.Scene);
        }

        public void ApplyInverse(ISceneMutator mutator, ISceneOrganizer organizer)
        {
            this.OldTransform.Apply(this.Node);
            _ = mutator.ReparentNode(this.Node.Id, this.NewParent?.Id, this.OldParent?.Id, this.Node.Scene);
            MoveToIndex(this.OldParent is null ? this.Node.Scene.RootNodes : this.OldParent.Children, this.Node, this.OldIndex);
            _ = organizer.RemoveNodeFromLayout(this.Node.Id, this.Node.Scene);
        }
    }

    private readonly record struct TransformSnapshot(Vector3 Position, Quaternion Rotation, Vector3 Scale)
    {
        public static TransformSnapshot Capture(TransformComponent? transform)
            => transform is null
                ? new TransformSnapshot(Vector3.Zero, Quaternion.Identity, Vector3.One)
                : new TransformSnapshot(transform.LocalPosition, transform.LocalRotation, transform.LocalScale);

        public void Apply(SceneNode node)
        {
            var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
            if (transform is null)
            {
                return;
            }

            transform.LocalPosition = this.Position;
            transform.LocalRotation = this.Rotation;
            transform.LocalScale = this.Scale;
        }
    }
}
