// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Logging methods for <see cref="DocumentHostViewModel" />.
/// </summary>
public partial class DocumentHostViewModel
{
    [LoggerMessage(EventId = 1, Level = LogLevel.Debug, Message = "AddNewDocumentAsync called. WindowId: {WindowId}")]
    private static partial void LogAddNewDocumentAsyncCalled(ILogger logger, object windowId);

    [Conditional("DEBUG")]
    private void LogAddNewDocumentAsyncCalled()
        => LogAddNewDocumentAsyncCalled(this.logger, this.windowId.Value);

    [LoggerMessage(EventId = 2, Level = LogLevel.Warning, Message = "Cannot add new document: WindowId is invalid.")]
    private static partial void LogCannotAddNewDocumentWindowIdInvalid(ILogger logger);

    private void LogCannotAddNewDocumentWindowIdInvalid()
        => LogCannotAddNewDocumentWindowIdInvalid(this.logger);

    [LoggerMessage(EventId = 3, Level = LogLevel.Debug, Message = "OnOpenSceneRequested called. Scene: {SceneName} ({SceneId}), WindowId: {WindowId}")]
    private static partial void LogOnOpenSceneRequested(ILogger logger, string sceneName, Guid sceneId, object windowId);

    [Conditional("DEBUG")]
    private void LogOnOpenSceneRequested(string sceneName, Guid sceneId)
        => LogOnOpenSceneRequested(this.logger, sceneName, sceneId, this.windowId.Value);

    [LoggerMessage(EventId = 4, Level = LogLevel.Warning, Message = "Cannot open scene: WindowId is invalid.")]
    private static partial void LogCannotOpenSceneWindowIdInvalid(ILogger logger);

    private void LogCannotOpenSceneWindowIdInvalid()
        => LogCannotOpenSceneWindowIdInvalid(this.logger);

    [LoggerMessage(EventId = 5, Level = LogLevel.Debug, Message = "OnDocumentOpened called. DocumentId: {DocumentId}, Type: {MetadataType}, WindowId: {WindowId}")]
    private static partial void LogOnDocumentOpened(ILogger logger, Guid documentId, string metadataType, object windowId);

    [Conditional("DEBUG")]
    private void LogOnDocumentOpened(Guid documentId, string metadataType)
        => LogOnDocumentOpened(this.logger, documentId, metadataType, this.windowId.Value);

    [LoggerMessage(EventId = 6, Level = LogLevel.Warning, Message = "Unknown metadata type: {MetadataType}")]
    private static partial void LogUnknownMetadataType(ILogger logger, string metadataType);

    private void LogUnknownMetadataType(string metadataType)
        => LogUnknownMetadataType(this.logger, metadataType);

    [LoggerMessage(EventId = 7, Level = LogLevel.Debug, Message = "Selecting editor for DocumentId: {DocumentId}")]
    private static partial void LogSelectingEditor(ILogger logger, Guid documentId);

    [Conditional("DEBUG")]
    private void LogSelectingEditor(Guid documentId)
        => LogSelectingEditor(this.logger, documentId);

    [LoggerMessage(EventId = 8, Level = LogLevel.Error, Message = "Failed to create editor for DocumentId: {DocumentId}")]
    private static partial void LogFailedToCreateEditor(ILogger logger, Guid documentId);

    private void LogFailedToCreateEditor(Guid documentId)
        => LogFailedToCreateEditor(this.logger, documentId);

    [LoggerMessage(EventId = 9, Level = LogLevel.Debug, Message = "OnDocumentActivated called. DocumentId: {DocumentId}, WindowId: {WindowId}")]
    private static partial void LogOnDocumentActivated(ILogger logger, Guid documentId, object windowId);

    [Conditional("DEBUG")]
    private void LogOnDocumentActivated(Guid documentId)
        => LogOnDocumentActivated(this.logger, documentId, this.windowId.Value);

    [LoggerMessage(EventId = 10, Level = LogLevel.Warning, Message = "OnDocumentActivated: Editor not found for DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentActivatedEditorNotFound(ILogger logger, Guid documentId);

    private void LogOnDocumentActivatedEditorNotFound(Guid documentId)
        => LogOnDocumentActivatedEditorNotFound(this.logger, documentId);

    [LoggerMessage(EventId = 11, Level = LogLevel.Debug, Message = "DocumentHostViewModel initialized. WindowId: {WindowId}")]
    private static partial void LogInitialized(ILogger logger, object windowId);

    [Conditional("DEBUG")]
    private void LogInitialized()
        => LogInitialized(this.logger, this.windowId.Value);

    [LoggerMessage(EventId = 12, Level = LogLevel.Debug, Message = "Disposing DocumentHostViewModel. WindowId: {WindowId}")]
    private static partial void LogDisposing(ILogger logger, object windowId);

    private void LogDisposing()
        => LogDisposing(this.logger, this.windowId.Value);

    [LoggerMessage(EventId = 13, Level = LogLevel.Debug, Message = "OnDocumentClosed called. DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentClosed(ILogger logger, Guid documentId);

    private void LogOnDocumentClosed(Guid documentId)
        => LogOnDocumentClosed(this.logger, documentId);

    [LoggerMessage(EventId = 14, Level = LogLevel.Debug, Message = "Editor disposed for DocumentId: {DocumentId}")]
    private static partial void LogEditorDisposed(ILogger logger, Guid documentId);

    private void LogEditorDisposed(Guid documentId)
        => LogEditorDisposed(this.logger, documentId);

    [LoggerMessage(EventId = 15, Level = LogLevel.Debug, Message = "Reactivating existing scene document: {SceneName} ({SceneId})")]
    private static partial void LogReactivatingExistingScene(ILogger logger, string sceneName, Guid sceneId);

    [Conditional("DEBUG")]
    private void LogReactivatingExistingScene(string sceneName, Guid sceneId)
        => LogReactivatingExistingScene(this.logger, sceneName, sceneId);

    [LoggerMessage(EventId = 16, Level = LogLevel.Debug, Message = "Reactivated existing scene document: {SceneName} ({SceneId})")]
    private static partial void LogReactivatedExistingScene(ILogger logger, string sceneName, Guid sceneId);

    private void LogReactivatedExistingScene(string sceneName, Guid sceneId)
        => LogReactivatedExistingScene(this.logger, sceneName, sceneId);

    [LoggerMessage(EventId = 17, Level = LogLevel.Debug, Message = "Opened new scene document: {SceneName} ({SceneId})")]
    private static partial void LogOpenedNewScene(ILogger logger, string sceneName, Guid sceneId);

    [Conditional("DEBUG")]
    private void LogOpenedNewScene(string sceneName, Guid sceneId)
        => LogOpenedNewScene(this.logger, sceneName, sceneId);

    [LoggerMessage(EventId = 18, Level = LogLevel.Warning, Message = "Failed to release engine surfaces for DocumentId: {DocumentId}")]
    private static partial void LogSurfaceReleaseFailed(ILogger logger, Guid documentId, Exception exception);

    private void LogSurfaceReleaseFailed(Guid documentId, Exception exception)
        => LogSurfaceReleaseFailed(this.logger, documentId, exception);
}
