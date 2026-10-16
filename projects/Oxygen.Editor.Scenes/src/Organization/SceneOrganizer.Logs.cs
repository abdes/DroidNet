// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.SceneExplorer.Operations;

public partial class SceneOrganizer
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Folder '{FolderId}' not found. Available folders: {Folders}")]
    private static partial void LogFolderNotFound(ILogger logger, FolderId folderId, string folders);

    private void LogFolderNotFound(FolderId folderId, IEnumerable<FolderId> available)
        => LogFolderNotFound(this.logger, folderId, string.Join(", ", available));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "CreateFolder: Name={Name} ParentFolder={ParentFolder} ParentNode={ParentNode} FolderId={FolderId}")]
    private static partial void LogCreateFolder(ILogger logger, string name, FolderId? parentFolder, NodeId? parentNode, FolderId? folderId);

    private void LogCreateFolder(string name, FolderId? parentFolder, NodeId? parentNode, FolderId? folderId)
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
    private static partial void LogMoveNodeRemoved(ILogger logger, int count, NodeId nodeId);

    private void LogMoveNodeRemoved(int count, NodeId nodeId)
        => LogMoveNodeRemoved(this.logger, count, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveNodeToFolder: moved node {NodeId} into folder {FolderId}")]
    private static partial void LogMoveNodeToFolder(ILogger logger, NodeId nodeId, FolderId folderId);

    private void LogMoveNodeToFolder(NodeId nodeId, FolderId folderId)
        => LogMoveNodeToFolder(this.logger, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RemoveNodeFromFolder: removed node {NodeId} from folder {FolderId}")]
    private static partial void LogRemoveNodeFromFolder(ILogger logger, NodeId nodeId, FolderId folderId);

    private void LogRemoveNodeFromFolder(NodeId nodeId, FolderId folderId)
        => LogRemoveNodeFromFolder(this.logger, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RemoveNodeFromLayout: removed {Count} entries for node {NodeId}")]
    private static partial void LogRemoveNodeFromLayout(ILogger logger, int count, NodeId nodeId);

    private void LogRemoveNodeFromLayout(int count, NodeId nodeId)
        => LogRemoveNodeFromLayout(this.logger, count, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveFolderToParent: moved folder {FolderId} to parent {NewParentFolderId}")]
    private static partial void LogMoveFolderToParent(ILogger logger, FolderId folderId, FolderId? newParentFolderId);

    private void LogMoveFolderToParent(FolderId folderId, FolderId? newParentFolderId)
        => LogMoveFolderToParent(this.logger, folderId, newParentFolderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RemoveFolder: removed folder {FolderId}, promoteChildren={PromoteChildren}")]
    private static partial void LogRemoveFolder(ILogger logger, FolderId folderId, bool promoteChildren);

    private void LogRemoveFolder(FolderId folderId, bool promoteChildren)
        => LogRemoveFolder(this.logger, folderId, promoteChildren);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "RenameFolder: renamed folder {FolderId} to '{NewName}'")]
    private static partial void LogRenameFolder(ILogger logger, FolderId folderId, string newName);

    private void LogRenameFolder(FolderId folderId, string newName)
        => LogRenameFolder(this.logger, folderId, newName);
}
