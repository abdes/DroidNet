// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Services;

/// <summary>
/// Implementation of the <see cref="ISceneExplorerService"/>.
/// </summary>
public partial class SceneExplorerService : ISceneExplorerService
{
    private readonly ISceneMutator sceneMutator;
    private readonly ISceneOrganizer sceneOrganizer;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly ILogger<SceneExplorerService> logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneExplorerService"/> class.
    /// </summary>
    /// <param name="sceneMutator">The scene mutator for scene graph operations.</param>
    /// <param name="sceneOrganizer">The scene organizer for layout operations.</param>
    /// <param name="sceneEngineSync">The scene engine sync service.</param>
    /// <param name="logger">The logger.</param>
    public SceneExplorerService(
        ISceneMutator sceneMutator,
        ISceneOrganizer sceneOrganizer,
        ISceneEngineSync sceneEngineSync,
        ILogger<SceneExplorerService>? logger = null)
    {
        this.sceneMutator = sceneMutator;
        this.sceneOrganizer = sceneOrganizer;
        this.sceneEngineSync = sceneEngineSync;
        this.logger = logger ?? NullLogger<SceneExplorerService>.Instance;
    }

    /// <inheritdoc />
    public async Task<SceneNodeChangeRecord?> CreateNodeAsync(ITreeItem parent, string name)
    {
        var scene = this.GetScene(parent) ?? throw new InvalidOperationException("Could not resolve scene from parent.");
        var newNode = new SceneNode(scene) { Name = name };
        return await this.AddNodeAsync(parent, newNode).ConfigureAwait(false);
    }

    /// <inheritdoc />
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
            this.sceneOrganizer.MoveNodeToFolder(node.Id, folderAdapter.Id, scene);
        }

        if (change?.RequiresEngineSync == true)
        {
            await this.sceneEngineSync.CreateNodeAsync(node, change.NewParentId).ConfigureAwait(false);
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
            var node = nodeAdapter.AttachedObject;
            SceneNodeChangeRecord? change = null;

            // If the node belongs to a different scene instance (stale reference), we should try to find the equivalent node in the current scene.
            if (!ReferenceEquals(node.Scene, scene))
            {
                this.LogMoveItemNodeSceneMismatch(node, scene);

                // Find the node in the correct scene by ID
                var freshNode = FindNodeById(scene, node.Id);
                if (freshNode != null)
                {
                    this.LogMoveItemResolvedFreshNode(freshNode);
                    node = freshNode;
                }
                else
                {
                    this.LogMoveItemCouldNotResolveNode(node);

                    // We might still proceed, but it's risky. The Mutator might fail or operate on the wrong object.
                    // However, if we return null, the operation aborts.
                    // Let's try to proceed with the stale node if we can't find a fresh one, but it will likely fail later.
                }
            }

            if (newParent is SceneAdapter)
            {
                // Move to Root
                this.LogMoveItemMovingToRoot();
                change = this.sceneMutator.CreateNodeAtRoot(node, scene);
                this.sceneOrganizer.RemoveNodeFromLayout(node.Id, scene);
            }
            else if (newParent is SceneNodeAdapter targetNodeAdapter)
            {
                // Move to Node
                this.LogMoveItemMovingToNode(targetNodeAdapter);
                change = this.sceneMutator.CreateNodeUnderParent(node, targetNodeAdapter.AttachedObject, scene);
                this.sceneOrganizer.RemoveNodeFromLayout(node.Id, scene);
            }
            else if (newParent is FolderAdapter folderAdapter)
            {
                // Move to Folder
                this.LogMoveItemMovingToFolder(folderAdapter);

                // First, ensure the node is in the correct lineage (parented to the folder's scene parent).
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

                // Now that the node is in the correct lineage, we can move it into the folder.
                this.LogMoveItemMovedToFolder(node.Id, folderAdapter.Id);
                try
                {
                    this.sceneOrganizer.MoveNodeToFolder(node.Id, folderAdapter.Id, scene);
                }
                catch (Exception ex)
                {
                    this.LogMoveItemMoveToFolderFailed(ex, node.Id, folderAdapter.Id);
                    throw;
                }
            }

            if (change?.RequiresEngineSync == true)
            {
                await this.sceneEngineSync.ReparentNodeAsync(node.Id, change.NewParentId).ConfigureAwait(false);
            }

            return change;
        }
        else if (item is FolderAdapter folderAdapter)
        {
            if (newParent is FolderAdapter targetFolder)
            {
                this.sceneOrganizer.MoveFolderToParent(folderAdapter.Id, targetFolder.Id, scene);
            }
            else if (newParent is SceneAdapter)
            {
                this.sceneOrganizer.MoveFolderToParent(folderAdapter.Id, newParentFolderId: null, scene);
            }
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

            await this.MoveItemAsync(item, newParent, newIndex).ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<IList<SceneNodeChangeRecord>> DeleteItemsAsync(IEnumerable<ITreeItem> items)
    {
        var itemsList = items.ToList();
        var changes = new List<SceneNodeChangeRecord>();
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
                    await this.sceneEngineSync.RemoveNodeAsync(nodeAdapter.AttachedObject.Id).ConfigureAwait(false);
                }

                // Also remove from layout if present
                this.sceneOrganizer.RemoveNodeFromLayout(nodeAdapter.AttachedObject.Id, scene);
                changes.Add(change);
            }
            else if (item is FolderAdapter folderAdapter)
            {
                this.sceneOrganizer.RemoveFolder(folderAdapter.Id, promoteChildrenToParent: false, scene);
            }
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
        else if (item is FolderAdapter folderAdapter)
        {
            folderAdapter.Label = newName;
            var scene = this.GetScene(item);
            if (scene != null)
            {
                this.sceneOrganizer.RenameFolder(folderAdapter.Id, newName, scene);
            }
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
}
