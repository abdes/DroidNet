// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

public partial class SceneOrganizer
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Folder '{FolderId}' not found. Available folders: {Folders}")]
    private static partial void LogFolderNotFound(ILogger logger, System.Guid folderId, string folders);

    private void LogFolderNotFound(System.Guid folderId, IEnumerable<System.Guid> available)
        => LogFolderNotFound(this.logger, folderId, string.Join(", ", available));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "CreateFolder: Name={Name} ParentFolder={ParentFolder} ParentNode={ParentNode} FolderId={FolderId}")]
    private static partial void LogCreateFolder(ILogger logger, string name, System.Guid? parentFolder, System.Guid? parentNode, System.Guid? folderId);

    private void LogCreateFolder(string name, System.Guid? parentFolder, System.Guid? parentNode, System.Guid? folderId)
        => LogCreateFolder(this.logger, name, parentFolder, parentNode, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Operation}: Scene Hash={SceneHash}, Layout Count={LayoutCount}, Folder Count={FolderCount}")]
    private static partial void LogSceneInfoEvent(ILogger logger, string operation, int sceneHash, int layoutCount, int folderCount);

    private void LogSceneInfoEvent(string operation, int sceneHash, int layoutCount, int folderCount)
        => LogSceneInfoEvent(this.logger, operation, sceneHash, layoutCount, folderCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveNodeToFolder: removed {Count} entries for node {NodeId}")]
    private static partial void LogMoveNodeRemoved(ILogger logger, int count, System.Guid nodeId);

    private void LogMoveNodeRemoved(int count, System.Guid nodeId)
        => LogMoveNodeRemoved(this.logger, count, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveNodeToFolder: moved node {NodeId} into folder {FolderId}")]
    private static partial void LogMoveNodeToFolder(ILogger logger, System.Guid nodeId, System.Guid folderId);

    private void LogMoveNodeToFolder(System.Guid nodeId, System.Guid folderId)
        => LogMoveNodeToFolder(this.logger, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RemoveNodeFromFolder: removed node {NodeId} from folder {FolderId}")]
    private static partial void LogRemoveNodeFromFolder(ILogger logger, System.Guid nodeId, System.Guid folderId);

    private void LogRemoveNodeFromFolder(System.Guid nodeId, System.Guid folderId)
        => LogRemoveNodeFromFolder(this.logger, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RemoveNodeFromLayout: removed {Count} entries for node {NodeId}")]
    private static partial void LogRemoveNodeFromLayout(ILogger logger, int count, System.Guid nodeId);

    private void LogRemoveNodeFromLayout(int count, System.Guid nodeId)
        => LogRemoveNodeFromLayout(this.logger, count, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "ReconcileLayout: Starting reconcile for SceneHash={SceneHash}, LayoutCount={LayoutCount}")]
    private static partial void LogReconcileStart(ILogger logger, int sceneHash, int layoutCount);

    private void LogReconcileStart(int sceneHash, int layoutCount)
        => LogReconcileStart(this.logger, sceneHash, layoutCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "ReconcileLayout: Completed reconcile. UsedNodes={UsedNodes} UsedFolders={UsedFolders}")]
    private static partial void LogReconcileEnd(ILogger logger, int usedNodes, int usedFolders);

    private void LogReconcileEnd(int usedNodes, int usedFolders)
        => LogReconcileEnd(this.logger, usedNodes, usedFolders);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ReconcileLayout: Attaching missing root node {NodeId}")]
    private static partial void LogAttachingMissingRootNode(ILogger logger, System.Guid nodeId);

    private void LogAttachingMissingRootNode(System.Guid nodeId)
        => LogAttachingMissingRootNode(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AdapterIndex: Nodes={NodeAdapters} Folders={FolderAdapters} AllAdapters={AllAdapters}")]
    private static partial void LogAdapterIndexStats(ILogger logger, int nodeAdapters, int folderAdapters, int allAdapters);

    private void LogAdapterIndexStats(int nodeAdapters, int folderAdapters, int allAdapters)
        => LogAdapterIndexStats(this.logger, nodeAdapters, folderAdapters, allAdapters);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveFolderToParent: moved folder {FolderId} to parent {NewParentFolderId}")]
    private static partial void LogMoveFolderToParent(ILogger logger, System.Guid folderId, System.Guid? newParentFolderId);

    private void LogMoveFolderToParent(System.Guid folderId, System.Guid? newParentFolderId)
        => LogMoveFolderToParent(this.logger, folderId, newParentFolderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RemoveFolder: removed folder {FolderId}, promoteChildren={PromoteChildren}")]
    private static partial void LogRemoveFolder(ILogger logger, System.Guid folderId, bool promoteChildren);

    private void LogRemoveFolder(System.Guid folderId, bool promoteChildren)
        => LogRemoveFolder(this.logger, folderId, promoteChildren);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RenameFolder: renamed folder {FolderId} to '{NewName}'")]
    private static partial void LogRenameFolder(ILogger logger, System.Guid folderId, string newName);

    private void LogRenameFolder(System.Guid folderId, string newName)
        => LogRenameFolder(this.logger, folderId, newName);
}
