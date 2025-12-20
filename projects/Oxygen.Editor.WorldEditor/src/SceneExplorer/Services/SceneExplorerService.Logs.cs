// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>
///     Logging helpers for <see cref="SceneExplorerService"/>.
/// </summary>
public partial class SceneExplorerService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "CreateFolderAsync: Failed to create folder. Record NewFolder is null.")]
    private static partial void LogCreateFolderFailed(ILogger logger);

    private void LogCreateFolderFailed()
        => LogCreateFolderFailed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "CreateFolderAsync: Successfully created folder {FolderId} '{FolderName}'")]
    private static partial void LogCreateFolderCreated(ILogger logger, System.Guid folderId, string folderName);

    private void LogCreateFolderCreated(System.Guid folderId, string folderName)
        => LogCreateFolderCreated(this.logger, folderId, folderName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Item={Item} NewParent={NewParent} Index={Index}")]
    private static partial void LogMoveItemInvoked(ILogger logger, string item, string newParent, int index);

    [Conditional("DEBUG")]
    private void LogMoveItemInvoked(ITreeItem item, ITreeItem newParent, int index)
        => LogMoveItemInvoked(this.logger, item?.Label ?? "<null>", newParent?.Label ?? "<null>", index);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "MoveItemAsync: Node '{Node}' belongs to Scene {NodeSceneHash}, but target is in Scene {TargetSceneHash}. Attempting to resolve node in target scene.")]
    private static partial void LogMoveItemNodeSceneMismatch(ILogger logger, string node, int nodeSceneHash, int targetSceneHash);

    private void LogMoveItemNodeSceneMismatch(SceneNode node, Scene targetScene)
        => LogMoveItemNodeSceneMismatch(this.logger, node.Name ?? "<null>", node.Scene?.GetHashCode() ?? 0, targetScene?.GetHashCode() ?? 0);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveItemAsync: Resolved fresh node '{Node}' in target scene.")]
    private static partial void LogMoveItemResolvedFreshNode(ILogger logger, string node);

    private void LogMoveItemResolvedFreshNode(SceneNode node)
        => LogMoveItemResolvedFreshNode(this.logger, node.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "MoveItemAsync: Could not find node '{Node}' ({NodeId}) in target scene.")]
    private static partial void LogMoveItemCouldNotResolveNode(ILogger logger, string node, System.Guid nodeId);

    private void LogMoveItemCouldNotResolveNode(SceneNode node)
        => LogMoveItemCouldNotResolveNode(this.logger, node.Name ?? "<null>", node.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Moving to Root")]
    private static partial void LogMoveItemMovingToRoot(ILogger logger);

    [Conditional("DEBUG")]
    private void LogMoveItemMovingToRoot()
        => LogMoveItemMovingToRoot(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Moving to Node {Node}")]
    private static partial void LogMoveItemMovingToNode(ILogger logger, string node);

    [Conditional("DEBUG")]
    private void LogMoveItemMovingToNode(SceneNodeAdapter targetNode)
        => LogMoveItemMovingToNode(this.logger, targetNode?.Label ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Moving to Folder {Folder}")]
    private static partial void LogMoveItemMovingToFolder(ILogger logger, string folder);

    [Conditional("DEBUG")]
    private void LogMoveItemMovingToFolder(FolderAdapter folder)
        => LogMoveItemMovingToFolder(this.logger, folder?.Label ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Folder Scene Parent is {Parent}")]
    private static partial void LogMoveItemFolderSceneParent(ILogger logger, string parent);

    [Conditional("DEBUG")]
    private void LogMoveItemFolderSceneParent(ITreeItem? parent)
        => LogMoveItemFolderSceneParent(this.logger, parent?.Label ?? "null");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Reparenting to Root (SceneAdapter)")]
    private static partial void LogMoveItemReparentingToRoot(ILogger logger);

    [Conditional("DEBUG")]
    private void LogMoveItemReparentingToRoot()
        => LogMoveItemReparentingToRoot(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MoveItemAsync: Reparenting to Node {Node}")]
    private static partial void LogMoveItemReparentingToNode(ILogger logger, string node);

    [Conditional("DEBUG")]
    private void LogMoveItemReparentingToNode(SceneNodeAdapter node)
        => LogMoveItemReparentingToNode(this.logger, node?.Label ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "MoveItemAsync: Moving Node {NodeId} to Folder {FolderId}. Lineage check passed.")]
    private static partial void LogMoveItemMovedToFolder(ILogger logger, System.Guid nodeId, System.Guid folderId);

    private void LogMoveItemMovedToFolder(System.Guid nodeId, System.Guid folderId)
        => LogMoveItemMovedToFolder(this.logger, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "MoveItemAsync: Failed to move node {NodeId} to folder {FolderId}")]
    private static partial void LogMoveItemMoveToFolderFailed(ILogger logger, Exception ex, System.Guid nodeId, System.Guid folderId);

    private void LogMoveItemMoveToFolderFailed(Exception ex, System.Guid nodeId, System.Guid folderId)
        => LogMoveItemMoveToFolderFailed(this.logger, ex, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "GetScene: Item={Item} SceneHash={SceneHash}")]
    private static partial void LogGetSceneFound(ILogger logger, string item, int sceneHash);

    [Conditional("DEBUG")]
    private void LogGetSceneFound(ITreeItem item, Scene scene)
        => LogGetSceneFound(this.logger, item?.Label ?? "<null>", scene?.GetHashCode() ?? 0);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "MoveItemAsync: Could not resolve Scene for move operation.")]
    private static partial void LogMoveItemCouldNotResolveScene(ILogger logger);

    private void LogMoveItemCouldNotResolveScene()
        => LogMoveItemCouldNotResolveScene(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "GetScene: Could not find scene for item {Item}")]
    private static partial void LogGetSceneNotFound(ILogger logger, string item);

    private void LogGetSceneNotFound(ITreeItem item)
        => LogGetSceneNotFound(this.logger, item?.Label ?? "<null>");
}
