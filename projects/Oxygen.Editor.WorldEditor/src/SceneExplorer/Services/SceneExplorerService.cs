// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>
/// Implementation of the <see cref="ISceneExplorerService"/>.
/// </summary>
/// <param name="sceneMutator">The scene mutator.</param>
/// <param name="sceneOrganizer">The scene organizer.</param>
/// <param name="sceneEngineSync">The scene engine sync.</param>
/// <param name="operationResults">The operation results.</param>
/// <param name="statusReducer">The status reducer.</param>
/// <param name="logger">The logger.</param>
public partial class SceneExplorerService(
    ISceneMutator sceneMutator,
    ISceneOrganizer sceneOrganizer,
    ISceneEngineSync sceneEngineSync,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    ILogger<SceneExplorerService>? logger = null) : ISceneExplorerService
{
    private readonly ISceneMutator sceneMutator = sceneMutator;
    private readonly ISceneOrganizer sceneOrganizer = sceneOrganizer;
    private readonly ISceneEngineSync sceneEngineSync = sceneEngineSync;
    private readonly IOperationResultPublisher operationResults = operationResults;
    private readonly IStatusReducer statusReducer = statusReducer;
    private readonly ILogger<SceneExplorerService> logger = logger ?? NullLogger<SceneExplorerService>.Instance;

    /// <inheritdoc/>
    public event EventHandler<SceneAuthoringChangedEventArgs>? AuthoringChanged;

    /// <inheritdoc />
    public async Task<SceneNodeChangeRecord?> CreateNodeAsync(ITreeItem parent, string name)
    {
        var scene = this.GetScene(parent) ?? throw new InvalidOperationException("Could not resolve scene from parent.");
        var newNode = new SceneNode(scene) { Name = name };
        return await this.AddNodeAsync(parent, newNode).ConfigureAwait(false);
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Live synchronization failures are reported without rolling back authoring changes.")]
    public async Task<SceneNodeChangeRecord?> AddNodeAsync(ITreeItem parent, SceneNode node)
    {
        var scene = this.GetScene(parent) ?? throw new InvalidOperationException("Could not resolve scene from parent.");
        SceneNodeChangeRecord? change = null;

        if (parent is SceneAdapter)
        {
            change = this.sceneMutator.CreateNodeAtRoot(node, scene);
        }
        else if (parent is SceneNodeAdapter nodeAdapter)
        {
            change = this.sceneMutator.CreateNodeUnderParent(node, nodeAdapter.AttachedObject, scene);
        }
        else if (parent is FolderAdapter folderAdapter)
        {
            var sceneParentItem = FindSceneParent(folderAdapter);
            if (sceneParentItem is SceneAdapter)
            {
                change = this.sceneMutator.CreateNodeAtRoot(node, scene);
            }
            else if (sceneParentItem is SceneNodeAdapter sna)
            {
                change = this.sceneMutator.CreateNodeUnderParent(node, sna.AttachedObject, scene);
            }

            // Also update layout to put it in the folder
            _ = this.sceneOrganizer.MoveNodeToFolder(node.Id, folderAdapter.Id, scene);
        }

        this.NotifyAuthoringChanged(scene);
        if (change?.RequiresEngineSync == true)
        {
            try
            {
                await this.sceneEngineSync.CreateNodeAsync(node, change.NewParentId).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                this.LogAuthoringSyncFailed(ex, node.Id, "add");
                this.PublishLiveSyncWarning(
                    SceneOperationKinds.NodeCreate,
                    DiagnosticCodes.LiveSyncPrefix + "CREATE_NODE_FAILED",
                    "Scene was updated but live preview was not",
                    node,
                    ex);
            }
        }

        return change;
    }

    /// <inheritdoc />
    public Task<Guid> CreateFolderAsync(ITreeItem parent, string name, Guid? folderId = null)
    {
        var scene = this.GetScene(parent) ?? throw new InvalidOperationException("Could not resolve scene from parent.");
        LayoutChangeRecord record;
        if (parent is SceneAdapter)
        {
            record = this.sceneOrganizer.CreateFolder(parentFolderId: null, name, scene, folderId);
        }
        else if (parent is FolderAdapter folderAdapter)
        {
            record = this.sceneOrganizer.CreateFolder(folderAdapter.Id, name, scene, folderId);
        }
        else if (parent is SceneNodeAdapter nodeAdapter)
        {
            record = this.sceneOrganizer.CreateFolder(parentFolderId: null, name, scene, folderId, nodeAdapter.AttachedObject.Id);
        }
        else
        {
            throw new NotSupportedException($"Creating folders under {parent.GetType().Name} is not supported.");
        }

        if (record.NewFolder == null || record.NewFolder.FolderId == null)
        {
            this.LogCreateFolderFailed();
            throw new InvalidOperationException("Failed to create folder.");
        }

        this.LogCreateFolderCreated(record.NewFolder.FolderId.Value, record.NewFolder.Name ?? "<null>");
        this.NotifyAuthoringChanged(scene);
        return Task.FromResult(record.NewFolder.FolderId.Value);
    }

    /// <inheritdoc />
    public async Task<SceneNodeChangeRecord?> MoveItemAsync(ITreeItem item, ITreeItem newParent, int index)
    {
        this.LogMoveItemInvoked(item, newParent, index);

        // Prefer getting the scene from the target parent, as that is where the destination folder/structure exists.
        // If the item has a stale scene reference, using the parent's scene ensures we are operating on the current valid scene.
        var scene = this.GetScene(newParent) ?? this.GetScene(item);

        if (scene == null)
        {
            this.LogMoveItemCouldNotResolveScene();
            return null;
        }

        if (item is SceneNodeAdapter nodeAdapter)
        {
            return await this.MoveNodeAsync(scene, nodeAdapter, newParent).ConfigureAwait(true);
        }

        if (item is FolderAdapter folderAdapter)
        {
            if (newParent is FolderAdapter targetFolder)
            {
                _ = this.sceneOrganizer.MoveFolderToParent(folderAdapter.Id, targetFolder.Id, scene);
            }
            else if (newParent is SceneAdapter)
            {
                _ = this.sceneOrganizer.MoveFolderToParent(folderAdapter.Id, newParentFolderId: null, scene);
            }
        }

        if (item is FolderAdapter)
        {
            this.NotifyAuthoringChanged(scene);
        }

        return null;
    }

    /// <inheritdoc />
    public async Task UpdateMovedItemsAsync(TreeItemsMovedEventArgs args)
    {
        foreach (var move in args.Moves)
        {
            var item = move.Item;
            var newParent = item.Parent;
            if (newParent == null)
            {
                continue;
            }

            // Find index in new parent
            var children = await newParent.Children.ConfigureAwait(false);
            var newIndex = children.IndexOf(item);

            _ = await this.MoveItemAsync(item, newParent, newIndex).ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<IList<SceneNodeChangeRecord>> DeleteItemsAsync(IEnumerable<ITreeItem> items)
    {
        var itemsList = items.ToList();
        var changes = new List<SceneNodeChangeRecord>();
        List<SceneNode> pendingSync = [];
        if (itemsList.Count == 0)
        {
            return changes;
        }

        var scene = this.GetScene(itemsList[0]);
        if (scene == null)
        {
            return changes;
        }

        foreach (var item in itemsList)
        {
            if (item is SceneNodeAdapter nodeAdapter)
            {
                var change = this.sceneMutator.RemoveNode(nodeAdapter.AttachedObject.Id, scene);
                if (change.RequiresEngineSync)
                {
                    pendingSync.Add(nodeAdapter.AttachedObject);
                }

                // Also remove from layout if present
                _ = this.sceneOrganizer.RemoveNodeFromLayout(nodeAdapter.AttachedObject.Id, scene);
                changes.Add(change);
                this.NotifyAuthoringChanged(scene);
            }
            else if (item is FolderAdapter folderAdapter)
            {
                _ = this.sceneOrganizer.RemoveFolder(folderAdapter.Id, promoteChildrenToParent: false, scene);
                this.NotifyAuthoringChanged(scene);
            }
        }

        foreach (var node in pendingSync)
        {
            await this.SyncRemovedNodeAsync(node).ConfigureAwait(true);
        }

        return changes;
    }

    /// <inheritdoc />
    public Task RenameItemAsync(ITreeItem item, string newName)
    {
        if (item is SceneNodeAdapter nodeAdapter)
        {
            nodeAdapter.AttachedObject.Name = newName;
        }

        if (item is FolderAdapter folderAdapter)
        {
            folderAdapter.Label = newName;
            var scene = this.GetScene(item);
            if (scene != null)
            {
                _ = this.sceneOrganizer.RenameFolder(folderAdapter.Id, newName, scene);
            }
        }

        if (this.GetScene(item) is { } modifiedScene)
        {
            this.NotifyAuthoringChanged(modifiedScene);
        }

        return Task.CompletedTask;
    }

    private static SceneNode? FindNodeById(Scene scene, Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            var found = FindInTree(root);
            if (found != null)
            {
                return found;
            }
        }

        return null;

        SceneNode? FindInTree(SceneNode node)
        {
            if (node.Id == nodeId)
            {
                return node;
            }

            foreach (var child in node.Children)
            {
                var found = FindInTree(child);
                if (found != null)
                {
                    return found;
                }
            }

            return null;
        }
    }

    private static ITreeItem? FindSceneParent(FolderAdapter item)
    {
        var current = item.Parent;
        while (current != null)
        {
            if (current is SceneAdapter or SceneNodeAdapter)
            {
                return current;
            }

            current = current.Parent;
        }

        return null;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Live synchronization failures are reported without rolling back authoring changes.")]
    private async Task<SceneNodeChangeRecord?> MoveNodeAsync(Scene scene, SceneNodeAdapter nodeAdapter, ITreeItem newParent)
    {
        var node = this.ResolveMovingNode(scene, nodeAdapter.AttachedObject);
        var change = this.MoveNodeModel(scene, node, newParent);
        this.NotifyAuthoringChanged(scene);
        if (change?.RequiresEngineSync == true)
        {
            try
            {
                await this.sceneEngineSync.ReparentNodeAsync(node.Id, change.NewParentId).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                this.LogAuthoringSyncFailed(ex, node.Id, "move");
                this.PublishLiveSyncWarning(
                    SceneOperationKinds.NodeReparent,
                    DiagnosticCodes.LiveSyncPrefix + "REPARENT_NODE_FAILED",
                    "Scene was updated but live preview was not",
                    node,
                    ex);
            }
        }

        return change;
    }

    private SceneNode ResolveMovingNode(Scene scene, SceneNode node)
    {
        if (!ReferenceEquals(node.Scene, scene))
        {
            this.LogMoveItemNodeSceneMismatch(node, scene);

            var freshNode = FindNodeById(scene, node.Id);
            if (freshNode != null)
            {
                this.LogMoveItemResolvedFreshNode(freshNode);
                node = freshNode;
            }
            else
            {
                this.LogMoveItemCouldNotResolveNode(node);
            }
        }

        return node;
    }

    private SceneNodeChangeRecord? MoveNodeModel(Scene scene, SceneNode node, ITreeItem newParent)
    {
        SceneNodeChangeRecord? change = null;
        if (newParent is SceneAdapter)
        {
            this.LogMoveItemMovingToRoot();
            change = this.sceneMutator.CreateNodeAtRoot(node, scene);
            _ = this.sceneOrganizer.RemoveNodeFromLayout(node.Id, scene);
        }
        else if (newParent is SceneNodeAdapter targetNodeAdapter)
        {
            this.LogMoveItemMovingToNode(targetNodeAdapter);
            change = this.sceneMutator.CreateNodeUnderParent(node, targetNodeAdapter.AttachedObject, scene);
            _ = this.sceneOrganizer.RemoveNodeFromLayout(node.Id, scene);
        }
        else if (newParent is FolderAdapter folderAdapter)
        {
            this.LogMoveItemMovingToFolder(folderAdapter);

            var sceneParentItem = FindSceneParent(folderAdapter);
            this.LogMoveItemFolderSceneParent(sceneParentItem);

            if (sceneParentItem is SceneAdapter)
            {
                this.LogMoveItemReparentingToRoot();
                change = this.sceneMutator.CreateNodeAtRoot(node, scene);
            }
            else if (sceneParentItem is SceneNodeAdapter sna)
            {
                this.LogMoveItemReparentingToNode(sna);
                change = this.sceneMutator.CreateNodeUnderParent(node, sna.AttachedObject, scene);
            }

            this.LogMoveItemMovedToFolder(node.Id, folderAdapter.Id);
            try
            {
                _ = this.sceneOrganizer.MoveNodeToFolder(node.Id, folderAdapter.Id, scene);
            }
            catch (Exception ex)
            {
                this.LogMoveItemMoveToFolderFailed(ex, node.Id, folderAdapter.Id);
                throw;
            }
        }

        return change;
    }

    private void NotifyAuthoringChanged(Scene scene) => this.AuthoringChanged?.Invoke(this, new SceneAuthoringChangedEventArgs(scene));

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Failed live synchronization does not roll back committed authoring state.")]
    private async Task SyncRemovedNodeAsync(SceneNode node)
    {
        try
        {
            await this.sceneEngineSync.RemoveNodeAsync(node.Id).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.PublishLiveSyncWarning(SceneOperationKinds.NodeDelete, DiagnosticCodes.LiveSyncPrefix + "REMOVE_NODE_FAILED", "Scene was updated but live preview was not", node, exception);
        }
    }

    private Scene? GetScene(ITreeItem item)
    {
        Scene? scene = null;
        if (item is SceneAdapter sa)
        {
            scene = sa.AttachedObject;
        }
        else if (item is SceneNodeAdapter sna)
        {
            scene = sna.AttachedObject.Scene;
        }
        else
        {
            var parent = item.Parent;
            while (parent != null)
            {
                if (parent is SceneAdapter pSa)
                {
                    scene = pSa.AttachedObject;
                    break;
                }

                if (parent is SceneNodeAdapter pSna)
                {
                    scene = pSna.AttachedObject.Scene;
                    break;
                }

                parent = parent.Parent;
            }
        }

        if (scene != null)
        {
            this.LogGetSceneFound(item, scene);
        }
        else
        {
            this.LogGetSceneNotFound(item);
        }

        return scene;
    }

    private void PublishLiveSyncWarning(
        string operationKind,
        string code,
        string title,
        SceneNode node,
        Exception exception)
        => SceneOperationResults.PublishWarning(
            this.operationResults,
            this.statusReducer,
            operationKind,
            FailureDomain.LiveSync,
            code,
            title,
            $"The scene node '{node.Name}' was changed in the authoring model, but the live preview did not update.",
            new AffectedScope
            {
                SceneId = node.Scene.Id,
                SceneName = node.Scene.Name,
                NodeId = node.Id,
                NodeName = node.Name,
            },
            exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene node {NodeId} authoring {Action} succeeded, but live sync failed.")]
    private partial void LogAuthoringSyncFailed(Exception exception, Guid nodeId, string action);
}
