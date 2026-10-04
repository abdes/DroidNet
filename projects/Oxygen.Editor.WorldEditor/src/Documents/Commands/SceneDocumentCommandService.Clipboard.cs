// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;

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
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
        }

        var topLevelIds = this.sceneOrganizer.FilterTopLevelSelectedNodeIds([.. nodeIds], context.Scene);
        if (topLevelIds.Count == 0)
        {
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
        }

        SceneNode? parentNode = null;
        if (newParentNodeId.HasValue)
        {
            parentNode = FindNode(context.Scene, newParentNodeId.Value);
            if (parentNode is null)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
            }
        }

        Guid? folderSceneParentNodeId = null;
        if (newParentFolderId.HasValue)
        {
            var (found, nodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, newParentFolderId.Value);
            if (!found)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
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
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
            }

            parentNode = anchor.Parent;
            insertIndex = anchor.Parent is null
                ? context.Scene.RootNodes.IndexOf(anchor) + 1
                : anchor.Parent.Children.IndexOf(anchor) + 1;
        }

        // Resolve every source up front so a stale id cannot leave a partially committed batch.
        var sources = new List<SceneNode>(topLevelIds.Count);
        foreach (var nodeId in topLevelIds)
        {
            var source = FindNode(context.Scene, nodeId);
            if (source is null)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
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
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
        }

        if (rootData.Count == 0)
        {
            return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
        }

        SceneNode? parentNode = null;
        if (newParentNodeId.HasValue)
        {
            parentNode = FindNode(context.Scene, newParentNodeId.Value);
            if (parentNode is null)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
            }
        }

        Guid? folderSceneParentNodeId = null;
        if (newParentFolderId.HasValue)
        {
            var (found, nodeId) = FindFolderSceneParentNodeId(context.Scene.ExplorerLayout, newParentFolderId.Value);
            if (!found)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
            }

            folderSceneParentNodeId = nodeId;
        }

        var insertIndex = -1;
        if (insertAfterNodeId.HasValue)
        {
            var anchor = FindNode(context.Scene, insertAfterNodeId.Value);
            if (anchor is null)
            {
                return SceneCommandResults.Failure<IReadOnlyList<SceneNode>>();
            }

            parentNode = anchor.Parent;
            insertIndex = anchor.Parent is null
                ? context.Scene.RootNodes.IndexOf(anchor) + 1
                : anchor.Parent.Children.IndexOf(anchor) + 1;
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

    private async Task SyncNodeSubtreeAsync(SceneDocumentCommandContext context, SceneNode node)
    {
        await this.SyncCreateNodeAsync(context, node, node.Parent?.Id).ConfigureAwait(true);
        foreach (var child in node.Children)
        {
            await this.SyncNodeSubtreeAsync(context, child).ConfigureAwait(true);
        }
    }

    private static SceneNodeData RemapNodeIds(SceneNodeData data)
        => data with
        {
            Id = Guid.NewGuid(),
            Components = data.Components.Select(RemapComponentId).ToList(),
            Children = data.Children?.Select(RemapNodeIds).ToList(),
        };

    private static ComponentData RemapComponentId(ComponentData data)
        => data switch
        {
            // Duplicated lights must not steal the source's Primary/Secondary atmosphere role.
            DirectionalLightData directional => directional with { Id = Guid.NewGuid(), AtmosphereSlot = AtmosphereLightSlot.None },
            _ => data with { Id = Guid.NewGuid() },
        };
}
