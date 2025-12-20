// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Documents;

/// <summary>
///     Logging methods for <see cref="DocumentHostViewModel" />.
/// </summary>
public partial class DocumentHostViewModel
{
    [LoggerMessage(EventId = 5, Level = LogLevel.Debug, Message = "OnDocumentOpened called. DocumentId: {DocumentId}, Type: {MetadataType}")]
    private static partial void LogOnDocumentOpened(ILogger logger, Guid documentId, string metadataType);

    [Conditional("DEBUG")]
    private void LogOnDocumentOpened(Guid documentId, string metadataType)
        => LogOnDocumentOpened(this.logger, documentId, metadataType);

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

    [LoggerMessage(EventId = 9, Level = LogLevel.Debug, Message = "OnDocumentActivated called. DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentActivated(ILogger logger, Guid documentId);

    [Conditional("DEBUG")]
    private void LogOnDocumentActivated(Guid documentId)
        => LogOnDocumentActivated(this.logger, documentId);

    [LoggerMessage(EventId = 10, Level = LogLevel.Warning, Message = "OnDocumentActivated: Editor not found for DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentActivatedEditorNotFound(ILogger logger, Guid documentId);

    private void LogOnDocumentActivatedEditorNotFound(Guid documentId)
        => LogOnDocumentActivatedEditorNotFound(this.logger, documentId);

    [LoggerMessage(EventId = 11, Level = LogLevel.Debug, Message = "DocumentHostViewModel initialized.")]
    private static partial void LogInitialized(ILogger logger);

    [Conditional("DEBUG")]
    private void LogInitialized()
        => LogInitialized(this.logger);

    [LoggerMessage(EventId = 12, Level = LogLevel.Debug, Message = "Disposing DocumentHostViewModel.")]
    private static partial void LogDisposing(ILogger logger);

    private void LogDisposing()
        => LogDisposing(this.logger);

    [LoggerMessage(EventId = 13, Level = LogLevel.Debug, Message = "OnDocumentClosed called. DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentClosed(ILogger logger, Guid documentId);

    private void LogOnDocumentClosed(Guid documentId)
        => LogOnDocumentClosed(this.logger, documentId);

    [LoggerMessage(EventId = 14, Level = LogLevel.Debug, Message = "Editor disposed for DocumentId: {DocumentId}")]
    private static partial void LogEditorDisposed(ILogger logger, Guid documentId);

    private void LogEditorDisposed(Guid documentId)
        => LogEditorDisposed(this.logger, documentId);

    [LoggerMessage(EventId = 18, Level = LogLevel.Warning, Message = "Failed to release engine surfaces for DocumentId: {DocumentId}")]
    private static partial void LogSurfaceReleaseFailed(ILogger logger, Guid documentId, Exception exception);

    private void LogSurfaceReleaseFailed(Guid documentId, Exception exception)
        => LogSurfaceReleaseFailed(this.logger, documentId, exception);
}
