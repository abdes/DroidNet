// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>Logging helpers for <see cref="SceneExplorerViewModel"/>.</summary>
public partial class SceneExplorerViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Scene Explorer selection update failed for document {DocumentId}.")]
    private static partial void LogSelectionApplyFailed(ILogger logger, Exception exception, DocumentId documentId);

    private void LogSelectionApplyFailed(Exception exception, DocumentId documentId)
        => LogSelectionApplyFailed(this.logger, exception, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Scene Explorer projection update failed for document {DocumentId}.")]
    private static partial void LogProjectionUpdateFailed(ILogger logger, Exception exception, DocumentId documentId);

    private void LogProjectionUpdateFailed(Exception exception, DocumentId documentId)
        => LogProjectionUpdateFailed(this.logger, exception, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Scene Explorer could not read what the instances of {Source} contain in document {DocumentId}.")]
    private static partial void LogInstanceStructureReadFailed(ILogger logger, Exception exception, AssetPath source, DocumentId documentId);

    private void LogInstanceStructureReadFailed(Exception exception, AssetPath source)
        => LogInstanceStructureReadFailed(this.logger, exception, source, this.SceneDocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A drop was refused in document {DocumentId}: it moves a node of instance {InstanceRoot} or gives one a child")]
    private static partial void LogInstancedDropRefused(ILogger logger, DocumentId documentId, NodeId? instanceRoot);

    private void LogInstancedDropRefused(NodeId? instanceRoot)
        => LogInstancedDropRefused(this.logger, this.SceneDocumentId, instanceRoot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Scene Explorer search failed for query {SearchQuery}.")]
    private static partial void LogSearchFailed(ILogger logger, Exception exception, string searchQuery);

    private void LogSearchFailed(Exception exception, string searchQuery)
        => LogSearchFailed(this.logger, exception, searchQuery);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply scene explorer add for {Item}.")]
    private static partial void LogAuthoringAddFailed(ILogger logger, Exception exception, string item);

    private void LogAuthoringAddFailed(Exception exception, string item)
        => LogAuthoringAddFailed(this.logger, exception, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load scene {SceneId} ({SceneName}).")]
    private static partial void LogAuthoringLoadFailed(ILogger logger, Exception exception, SceneId sceneId, string sceneName);

    private void LogAuthoringLoadFailed(Exception exception, SceneId sceneId, string sceneName)
        => LogAuthoringLoadFailed(this.logger, exception, sceneId, sceneName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply in-place rename for {Item}.")]
    private static partial void LogAuthoringRenameFailed(ILogger logger, Exception exception, string item);

    private void LogAuthoringRenameFailed(Exception exception, string item)
        => LogAuthoringRenameFailed(this.logger, exception, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolder invoked. SelectionModelType={Type}, ShownItemsCount={Count}")]
    private static partial void LogCreateFolderInvoked(ILogger logger, string? type, int count);

    [Conditional("DEBUG")]
    private void LogCreateFolderInvoked(object? selectionModel, int count)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var type = selectionModel?.GetType().Name;
        LogCreateFolderInvoked(this.logger, type, count);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection captured selected ids: [{Ids}]")]
    private static partial void LogCreateFolderCapturedSelection(ILogger logger, string ids);

    [Conditional("DEBUG")]
    private void LogCreateFolderCapturedSelection(string ids)
        => LogCreateFolderCapturedSelection(this.logger, ids);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: layout entries (top-level={TopCount}, total={TotalCount})")]
    private static partial void LogCreateFolderLayoutInfo(ILogger logger, int topCount, int totalCount);

    [Conditional("DEBUG")]
    private void LogCreateFolderLayoutInfo(int topCount, int totalCount)
        => LogCreateFolderLayoutInfo(this.logger, topCount, totalCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: Removed {Count} entries into folder")]
    private static partial void LogCreateFolderRemovedEntriesCount(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogCreateFolderRemovedEntriesCount(int count)
        => LogCreateFolderRemovedEntriesCount(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: SelectionModel yielded no items, falling back to ShownItems (found {Count} selected)")]
    private static partial void LogCreateFolderUsingShownItems(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogCreateFolderUsingShownItems(int count)
        => LogCreateFolderUsingShownItems(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection aborted: no selectable nodes found")]
    private static partial void LogCreateFolderNoSelection(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCreateFolderNoSelection()
        => LogCreateFolderNoSelection(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: node adapter for {NodeId} not found when moving into folder")]
    private static partial void LogCreateFolderNodeAdapterNotFound(ILogger logger, NodeId nodeId);

    [Conditional("DEBUG")]
    private void LogCreateFolderNodeAdapterNotFound(NodeId nodeId)
        => LogCreateFolderNodeAdapterNotFound(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: moved {Count} adapters into folder")]
    private static partial void LogCreateFolderMovedAdaptersCount(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogCreateFolderMovedAdaptersCount(int count)
        => LogCreateFolderMovedAdaptersCount(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created folder '{FolderName}' with {ChildCount} entries in scene {SceneId}")]
    private static partial void LogCreateFolderCreated(ILogger logger, string folderName, int childCount, SceneId sceneId);

    private void LogCreateFolderCreated(string folderName, int childCount, SceneId sceneId)
        => LogCreateFolderCreated(this.logger, folderName, childCount, sceneId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Undo remove failed for item '{Item}'.")]
    private static partial void LogUndoRemoveFailed(ILogger logger, Exception ex, string item);

    private void LogUndoRemoveFailed(Exception ex, string item)
        => LogUndoRemoveFailed(this.logger, ex, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot undo add because original parent was null for item '{Item}'.")]
    private static partial void LogCannotUndoAddOriginalParentNull(ILogger logger, string item);

    private void LogCannotUndoAddOriginalParentNull(string item)
        => LogCannotUndoAddOriginalParentNull(this.logger, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Undo add failed for item '{Item}'.")]
    private static partial void LogUndoAddFailed(ILogger logger, Exception ex, string item);

    private void LogUndoAddFailed(Exception ex, string item)
        => LogUndoAddFailed(this.logger, ex, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Unable to resolve scene for added item '{Item}'.")]
    private static partial void LogUnableResolveSceneForAddedItem(ILogger logger, string item);

    private void LogUnableResolveSceneForAddedItem(string item)
        => LogUnableResolveSceneForAddedItem(this.logger, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Move to folder rejected for node {NodeId} into folder {FolderId}.")]
    private static partial void LogMoveNodeToFolderRejected(ILogger logger, Exception ex, NodeId nodeId, FolderId folderId);

    private void LogMoveNodeToFolderRejected(Exception ex, NodeId nodeId, FolderId folderId)
        => LogMoveNodeToFolderRejected(this.logger, ex, nodeId, folderId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Unable to resolve scene for removed item '{Item}'.")]
    private static partial void LogUnableResolveSceneForRemovedItem(ILogger logger, string item);

    private void LogUnableResolveSceneForRemovedItem(string item)
        => LogUnableResolveSceneForRemovedItem(this.logger, item);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Created folder '{FolderName}' was inserted into layout but no matching folder adapter was found.")]
    private static partial void LogCreatedFolderAdapterNotFound(ILogger logger, string folderName);

    [Conditional("DEBUG")]
    private void LogCreatedFolderAdapterNotFound(string? folderName)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var name = folderName ?? "<null>";
        LogCreatedFolderAdapterNotFound(this.logger, name);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Auto-expanded and selected new folder '{FolderName}' ({FolderId}).")]
    private static partial void LogAutoExpandedSelectedFolder(ILogger logger, string folderName, string folderId);

    [Conditional("DEBUG")]
    private void LogAutoExpandedSelectedFolder(string? folderName, FolderId? folderId)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var name = folderName ?? "<null>";
        var id = folderId.HasValue ? folderId.Value.ToString() : "<null>";
        LogAutoExpandedSelectedFolder(this.logger, name, id);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateFolderFromSelection: moved adapter {Label} into folder {FolderName}.")]
    private static partial void LogCreateFolderMovedAdapter(ILogger logger, string label, string folderName);

    [Conditional("DEBUG")]
    private void LogCreateFolderMovedAdapter(string label, string folderName)
        => LogCreateFolderMovedAdapter(this.logger, label, folderName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{Label}: layout entries = [{Entries}].")]
    private static partial void LogLayoutEntries(ILogger logger, string label, string entries);

    [Conditional("DEBUG")]
    private void LogLayoutEntries(string label, string entries)
        => LogLayoutEntries(this.logger, label, entries);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot reparent node '{NodeName}' in engine because the node is not active.")]
    private static partial void LogCannotReparentNodeNotActive(ILogger logger, string nodeName);

    private void LogCannotReparentNodeNotActive(string nodeName)
        => LogCannotReparentNodeNotActive(this.logger, nodeName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "No scene addition action for change '{OperationName}'.")]
    private static partial void LogNoSceneAdditionAction(ILogger logger, string operationName);

    [Conditional("DEBUG")]
    private void LogNoSceneAdditionAction(string operationName)
        => LogNoSceneAdditionAction(this.logger, operationName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply scene change '{OperationName}' for node '{NodeName}' ({NodeId}).")]
    private static partial void LogFailedApplySceneChange(ILogger logger, Exception ex, string operationName, string nodeName, string nodeId);

    private void LogFailedApplySceneChange(Exception ex, string operationName, string? nodeName, NodeId? nodeId)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var name = nodeName ?? "<null>";
        var id = nodeId.HasValue ? nodeId.Value.ToString() : "<null>";
        LogFailedApplySceneChange(this.logger, ex, operationName, name, id);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Skipping removal handler for change '{OperationName}'.")]
    private static partial void LogSkippingRemovalHandler(ILogger logger, string operationName);

    [Conditional("DEBUG")]
    private void LogSkippingRemovalHandler(string operationName)
        => LogSkippingRemovalHandler(this.logger, operationName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot remove node '{NodeName}' from engine because the node is not active.")]
    private static partial void LogCannotRemoveNodeNotActive(ILogger logger, string nodeName);

    private void LogCannotRemoveNodeNotActive(string nodeName)
        => LogCannotRemoveNodeNotActive(this.logger, nodeName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to finalize removal for node '{NodeName}' ({NodeId}).")]
    private static partial void LogFailedFinalizeRemoval(ILogger logger, Exception ex, string nodeName, NodeId nodeId);

    private void LogFailedFinalizeRemoval(Exception ex, string nodeName, NodeId nodeId)
        => LogFailedFinalizeRemoval(this.logger, ex, nodeName, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Organizer returned no folder for folder creation.")]
    private static partial void LogOrganizerReturnedNullFolder(ILogger logger);

    private void LogOrganizerReturnedNullFolder()
        => LogOrganizerReturnedNullFolder(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot apply layout change because there is no active scene.")]
    private static partial void LogCannotApplyLayoutNoActiveScene(ILogger logger);

    private void LogCannotApplyLayoutNoActiveScene()
        => LogCannotApplyLayoutNoActiveScene(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        EventName = $"ui-{nameof(SceneExplorerViewModel)}-ItemAdded",
        Level = LogLevel.Information,
        Message = "Item added: '{ItemName}'.")]
    private static partial void LogItemAdded(ILogger logger, string itemName);

    private void LogItemAdded(string itemName)
        => LogItemAdded(this.logger, itemName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        EventName = $"ui-{nameof(SceneExplorerViewModel)}-ItemRemoved",
        Level = LogLevel.Information,
        Message = "Item removed: '{ItemName}'.")]
    private static partial void LogItemRemoved(ILogger logger, string itemName);

    private void LogItemRemoved(string itemName)
        => LogItemRemoved(this.logger, itemName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to perform batch removal of scene nodes. Falling back to individual removal.")]
    private static partial void LogBatchRemovalFailed(ILogger logger, Exception ex);

    private void LogBatchRemovalFailed(Exception ex)
        => LogBatchRemovalFailed(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "LoadSceneAsync: loaded scene {SceneId} (Name={SceneName})")]
    private static partial void LogSceneLoaded(ILogger logger, SceneId sceneId, string sceneName);

    private void LogSceneLoaded(SceneId sceneId, string? sceneName)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = sceneName ?? "<unnamed>";
        LogSceneLoaded(this.logger, sceneId, name);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Scene explorer switching to active document {DocumentId}")]
    private static partial void LogDocumentActivated(ILogger logger, DroidNet.Documents.DocumentId documentId);

    private void LogDocumentActivated(DroidNet.Documents.DocumentId documentId)
        => LogDocumentActivated(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "{Action} is unavailable: no workspace interaction service is composed.")]
    private static partial void LogWorkspaceStateUnavailable(ILogger logger, string action);

    private void LogWorkspaceStateUnavailable(string action)
        => LogWorkspaceStateUnavailable(this.logger, action);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Scene '{Scene}' was not renamed to '{Name}': {Reason}")]
    private static partial void LogSceneRenameRejected(ILogger logger, string scene, string name, string reason);

    private void LogSceneRenameRejected(string scene, string name, string reason)
        => LogSceneRenameRejected(this.logger, scene, name, reason);
}
