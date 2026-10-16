// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Documents;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>
///     Logging methods for <see cref="DocumentHostViewModel" />.
/// </summary>
public partial class DocumentHostViewModel
{
    [LoggerMessage(EventId = 5, Level = LogLevel.Debug, Message = "OnDocumentOpened called. DocumentId: {DocumentId}, Type: {MetadataType}")]
    private static partial void LogOnDocumentOpened(ILogger logger, DocumentId documentId, string metadataType);

    [Conditional("DEBUG")]
    private void LogOnDocumentOpened(DocumentId documentId, string metadataType)
        => LogOnDocumentOpened(this.logger, documentId, metadataType);

    [LoggerMessage(EventId = 6, Level = LogLevel.Warning, Message = "Unknown metadata type: {MetadataType}")]
    private static partial void LogUnknownMetadataType(ILogger logger, string metadataType);

    private void LogUnknownMetadataType(string metadataType)
        => LogUnknownMetadataType(this.logger, metadataType);

    [LoggerMessage(EventId = 7, Level = LogLevel.Debug, Message = "Selecting editor for DocumentId: {DocumentId}")]
    private static partial void LogSelectingEditor(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogSelectingEditor(DocumentId documentId)
        => LogSelectingEditor(this.logger, documentId);

    [LoggerMessage(EventId = 8, Level = LogLevel.Error, Message = "Failed to create editor for DocumentId: {DocumentId}")]
    private static partial void LogFailedToCreateEditor(ILogger logger, DocumentId documentId);

    private void LogFailedToCreateEditor(DocumentId documentId)
        => LogFailedToCreateEditor(this.logger, documentId);

    [LoggerMessage(EventId = 9, Level = LogLevel.Debug, Message = "OnDocumentActivated called. DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentActivated(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogOnDocumentActivated(DocumentId documentId)
        => LogOnDocumentActivated(this.logger, documentId);

    [LoggerMessage(EventId = 10, Level = LogLevel.Warning, Message = "OnDocumentActivated: Editor not found for DocumentId: {DocumentId}")]
    private static partial void LogOnDocumentActivatedEditorNotFound(ILogger logger, DocumentId documentId);

    private void LogOnDocumentActivatedEditorNotFound(DocumentId documentId)
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
    private static partial void LogOnDocumentClosed(ILogger logger, DocumentId documentId);

    private void LogOnDocumentClosed(DocumentId documentId)
        => LogOnDocumentClosed(this.logger, documentId);

    [LoggerMessage(EventId = 14, Level = LogLevel.Debug, Message = "Editor disposed for DocumentId: {DocumentId}")]
    private static partial void LogEditorDisposed(ILogger logger, DocumentId documentId);

    private void LogEditorDisposed(DocumentId documentId)
        => LogEditorDisposed(this.logger, documentId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to deactivate previous scene editor view during document switch.")]
    private partial void LogViewDeactivationFailed(Exception exception);
}
