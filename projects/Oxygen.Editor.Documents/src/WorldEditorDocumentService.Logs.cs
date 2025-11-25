// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Documents;

/// <summary>
///     Logging methods for <see cref="WorldEditorDocumentService" />.
/// </summary>
public partial class WorldEditorDocumentService
{
    [LoggerMessage(EventId = 1, Level = LogLevel.Debug, Message = "OpenDocumentAsync called for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogOpenDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogOpenDocumentCalled(object windowId, Guid documentId)
        => LogOpenDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 2, Level = LogLevel.Debug, Message = "CloseDocumentAsync called for WindowId: {WindowId}, DocumentId: {DocumentId}, Force: {Force}")]
    private static partial void LogCloseDocumentCalled(ILogger logger, object windowId, Guid documentId, bool force);

    [Conditional("DEBUG")]
    private void LogCloseDocumentCalled(object windowId, Guid documentId, bool force)
        => LogCloseDocumentCalled(this.logger, windowId, documentId, force);

    [LoggerMessage(EventId = 3, Level = LogLevel.Warning, Message = "CloseDocumentAsync: Document not found. WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogCloseDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogCloseDocumentNotFound(object windowId, Guid documentId)
        => LogCloseDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 4, Level = LogLevel.Debug, Message = "DocumentClosed for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDocumentClosed(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDocumentClosed(object windowId, Guid documentId)
        => LogDocumentClosed(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 5, Level = LogLevel.Debug, Message = "DetachDocumentAsync called for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDetachDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDetachDocumentCalled(object windowId, Guid documentId)
        => LogDetachDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 6, Level = LogLevel.Debug, Message = "Detached document for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDetached(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDetached(object windowId, Guid documentId)
        => LogDetached(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 7, Level = LogLevel.Warning, Message = "DetachDocumentAsync: Document not found. WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDetachDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogDetachDocumentNotFound(object windowId, Guid documentId)
        => LogDetachDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 8, Level = LogLevel.Debug, Message = "AttachDocumentAsync called for TargetWindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogAttachDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogAttachDocumentCalled(object windowId, Guid documentId)
        => LogAttachDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 9, Level = LogLevel.Debug, Message = "Attached document to WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogAttached(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogAttached(object windowId, Guid documentId)
        => LogAttached(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 10, Level = LogLevel.Debug, Message = "UpdateMetadataAsync called for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogUpdateMetadataCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogUpdateMetadataCalled(object windowId, Guid documentId)
        => LogUpdateMetadataCalled(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 11, Level = LogLevel.Warning, Message = "UpdateMetadataAsync: Document not found. WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogUpdateMetadataFailed(ILogger logger, object windowId, Guid documentId);

    private void LogUpdateMetadataFailed(object windowId, Guid documentId)
        => LogUpdateMetadataFailed(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 12, Level = LogLevel.Debug, Message = "UpdateMetadataAsync: Updated metadata for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogUpdateMetadataSuccess(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogUpdateMetadataSuccess(object windowId, Guid documentId)
        => LogUpdateMetadataSuccess(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 13, Level = LogLevel.Debug, Message = "SelectDocumentAsync called for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogSelectDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogSelectDocumentCalled(object windowId, Guid documentId)
        => LogSelectDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 14, Level = LogLevel.Warning, Message = "SelectDocumentAsync: Document not found. WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogSelectDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogSelectDocumentNotFound(object windowId, Guid documentId)
        => LogSelectDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 15, Level = LogLevel.Debug, Message = "DocumentActivated invoked for WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDocumentActivatedInvoked(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDocumentActivatedInvoked(object windowId, Guid documentId)
        => LogDocumentActivatedInvoked(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 16, Level = LogLevel.Warning, Message = "OpenDocumentAsync aborted: WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogDocumentOpenAborted(ILogger logger, object windowId, Guid documentId);

    private void LogDocumentOpenAborted(object windowId, Guid documentId)
        => LogDocumentOpenAborted(this.logger, windowId, documentId);

    [LoggerMessage(EventId = 17, Level = LogLevel.Warning, Message = "AttachDocumentAsync aborted: WindowId: {WindowId}, DocumentId: {DocumentId}")]
    private static partial void LogAttachDocumentAborted(ILogger logger, object windowId, Guid documentId);

    private void LogAttachDocumentAborted(object windowId, Guid documentId)
        => LogAttachDocumentAborted(this.logger, windowId, documentId);
}
