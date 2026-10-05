// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Deep-duplication authoring for <see cref="SceneDocumentCommandService"/>. Paste and copy-drop
/// create independent node/component identities while preserving asset references and material-slot
/// identities.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    /// <summary>Deep-copies node hierarchies and inserts them under a node, folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to duplicate.</param>
    /// <param name="newParentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="newParentFolderId">The destination folder for grouping, or <see langword="null"/> when not grouping.</param>
    /// <param name="insertAfterNodeId">Optional. If provided, the duplicated nodes will be inserted immediately after this sibling node; if <see langword="null"/> they are appended.</param>
    /// <returns>The created node roots.</returns>
    public async Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid? newParentNodeId,
        Guid? newParentFolderId,
        Guid? insertAfterNodeId = null)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "AUTHORING_SUSPENDED",
                "The scene is reloading or was replaced, so the duplication did not run.",
                context);
        }

        var topLevelIds = this.sceneOrganizer.FilterTopLevelSelectedNodeIds([.. nodeIds], context.Scene);
        if (topLevelIds.Count == 0)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "The nodes to duplicate no longer exist in the scene.",
                context);
        }

        SceneNode? parentNode = null;
        if (newParentNodeId.HasValue)
        {
            parentNode = FindNode(context.Scene, newParentNodeId.Value);
            if (parentNode is null)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The target parent node no longer exists.",
                    context);
            }
        }

        Guid? folderSceneParentNodeId = null;
        if (newParentFolderId.HasValue)
        {
            var (found, nodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, newParentFolderId.Value);
            if (!found)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The target folder no longer exists.",
                    context);
            }

            folderSceneParentNodeId = nodeId;
        }

        // Resolve the sibling anchor up front; inserting after a node implies its parent.
        var insertIndex = -1;
        if (insertAfterNodeId.HasValue)
        {
            var anchor = FindNode(context.Scene, insertAfterNodeId.Value);
            if (anchor is null)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The destination sibling no longer exists.",
                    context);
            }

            parentNode = anchor.Parent;
            insertIndex = anchor.Parent is null
                ? context.Scene.RootNodes.IndexOf(anchor) + 1
                : anchor.Parent.Children.IndexOf(anchor) + 1;
        }

        var destinationParent = parentNode
            ?? (folderSceneParentNodeId is { } parentId ? FindNode(context.Scene, parentId) : null);
        if (folderSceneParentNodeId.HasValue && destinationParent is null)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "The target folder's scene parent no longer exists.",
                context);
        }

        if (destinationParent is not null
            && this.RejectLockedTargets(context, SceneOperationKinds.NodeDuplicate, [destinationParent]) is { } lockFailure)
        {
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>(lockFailure.OperationResultId);
        }

        // Resolve every source up front so a stale id cannot leave a partially committed batch.
        var sources = new List<SceneNode>(topLevelIds.Count);
        foreach (var nodeId in topLevelIds)
        {
            var source = FindNode(context.Scene, nodeId);
            if (source is null)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "One or more copied nodes no longer exist.",
                    context);
            }

            sources.Add(source);
        }

        var created = new List<SceneNode>(sources.Count);
        context.History.BeginChangeSet($"Duplicate {sources.Count} node(s)");
        try
        {
            foreach (var source in sources)
            {
                var clone = SceneNode.CreateAndHydrate(context.Scene, RemapNodeIds(source.Dehydrate()));
                var actualParent = parentNode ?? (folderSceneParentNodeId.HasValue ? FindNode(context.Scene, folderSceneParentNodeId.Value) : null);
                _ = actualParent is null
                    ? this.sceneMutator.CreateNodeAtRoot(clone, context.Scene)
                    : this.sceneMutator.CreateNodeUnderParent(clone, actualParent, context.Scene);

                if (insertIndex >= 0)
                {
                    MoveToIndex(actualParent is null ? context.Scene.RootNodes : actualParent.Children, clone, insertIndex);
                    insertIndex++;
                }

                if (newParentFolderId.HasValue)
                {
                    EnsureExplorerLayout(context.Scene);
                    _ = this.sceneOrganizer.MoveNodeToFolder(clone.Id, newParentFolderId.Value, context.Scene);
                }

                this.RecordCreateNodeUndo(context, clone);
                this.PublishNodeAdded(context, clone);
                created.Add(clone);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        foreach (var clone in created)
        {
            await this.SyncNodeSubtreeAsync(context, clone).ConfigureAwait(true);
        }

        return SceneCommandResults.Success<IReadOnlyList<SceneNode>>(created);
    }

    /// <inheritdoc />
    public async Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesFromDataAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNodeData> rootData,
        Guid? newParentNodeId,
        Guid? newParentFolderId,
        Guid? insertAfterNodeId = null)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "AUTHORING_SUSPENDED",
                "The scene is reloading or was replaced, so the duplication did not run.",
                context);
        }

        if (rootData.Count == 0)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "The clipboard no longer contains any node data.",
                context);
        }

        SceneNode? parentNode = null;
        if (newParentNodeId.HasValue)
        {
            parentNode = FindNode(context.Scene, newParentNodeId.Value);
            if (parentNode is null)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The target parent node no longer exists.",
                    context);
            }
        }

        Guid? folderSceneParentNodeId = null;
        if (newParentFolderId.HasValue)
        {
            var (found, nodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, newParentFolderId.Value);
            if (!found)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The target folder no longer exists.",
                    context);
            }

            folderSceneParentNodeId = nodeId;
        }

        var insertIndex = -1;
        if (insertAfterNodeId.HasValue)
        {
            var anchor = FindNode(context.Scene, insertAfterNodeId.Value);
            if (anchor is null)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "The destination sibling no longer exists.",
                    context);
            }

            parentNode = anchor.Parent;
            insertIndex = anchor.Parent is null
                ? context.Scene.RootNodes.IndexOf(anchor) + 1
                : anchor.Parent.Children.IndexOf(anchor) + 1;
        }

        var destinationParent = parentNode
            ?? (folderSceneParentNodeId is { } parentId ? FindNode(context.Scene, parentId) : null);
        if (folderSceneParentNodeId.HasValue && destinationParent is null)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "The target folder's scene parent no longer exists.",
                context);
        }

        if (destinationParent is not null
            && this.RejectLockedTargets(context, SceneOperationKinds.NodeDuplicate, [destinationParent]) is { } lockFailure)
        {
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>(lockFailure.OperationResultId);
        }

        var created = new List<SceneNode>(rootData.Count);
        context.History.BeginChangeSet($"Duplicate {rootData.Count} node(s)");
        try
        {
            foreach (var data in rootData)
            {
                var clone = SceneNode.CreateAndHydrate(context.Scene, RemapNodeIds(data));
                var actualParent = parentNode ?? (folderSceneParentNodeId.HasValue ? FindNode(context.Scene, folderSceneParentNodeId.Value) : null);
                _ = actualParent is null
                    ? this.sceneMutator.CreateNodeAtRoot(clone, context.Scene)
                    : this.sceneMutator.CreateNodeUnderParent(clone, actualParent, context.Scene);

                if (insertIndex >= 0)
                {
                    MoveToIndex(actualParent is null ? context.Scene.RootNodes : actualParent.Children, clone, insertIndex);
                    insertIndex++;
                }

                if (newParentFolderId.HasValue)
                {
                    EnsureExplorerLayout(context.Scene);
                    _ = this.sceneOrganizer.MoveNodeToFolder(clone.Id, newParentFolderId.Value, context.Scene);
                }

                this.RecordCreateNodeUndo(context, clone);
                this.PublishNodeAdded(context, clone);
                created.Add(clone);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        foreach (var clone in created)
        {
            await this.SyncNodeSubtreeAsync(context, clone).ConfigureAwait(true);
        }

        return SceneCommandResults.Success<IReadOnlyList<SceneNode>>(created);
    }

    private static SceneNodeData RemapNodeIds(SceneNodeData data)
        => data with
        {
            Id = Guid.NewGuid(),
            Components = [.. data.Components.Select(RemapComponentId)],
            Children = data.Children?.Select(RemapNodeIds).ToList(),
        };

    private static ComponentData RemapComponentId(ComponentData data)
        => data switch
        {
            // Duplicated lights must not steal the source's Primary/Secondary atmosphere role.
            DirectionalLightData directional => directional with { Id = Guid.NewGuid(), AtmosphereSlot = AtmosphereLightSlot.None },
            _ => data with { Id = Guid.NewGuid() },
        };

    private SceneValueCommandResult<IReadOnlyList<SceneNode>> DuplicateFailure(
        string code,
        string message,
        SceneDocumentCommandContext context)
        => SceneCommandResults.Failure<IReadOnlyList<SceneNode>>(this.PublishSceneFailure(
            SceneOperationKinds.NodeDuplicate,
            code,
            "Nodes were not duplicated",
            message,
            context));

    private async Task SyncNodeSubtreeAsync(SceneDocumentCommandContext context, SceneNode node)
    {
        await this.SyncCreateNodeAsync(context, node, node.Parent?.Id).ConfigureAwait(true);
        foreach (var child in node.Children)
        {
            await this.SyncNodeSubtreeAsync(context, child).ConfigureAwait(true);
        }
    }
}
