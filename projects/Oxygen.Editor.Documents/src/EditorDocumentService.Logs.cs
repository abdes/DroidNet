// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Documents;

/// <summary>
///     Logging methods for <see cref="EditorDocumentService" />.
/// </summary>
public partial class EditorDocumentService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OpenDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogOpenDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogOpenDocumentCalled(object windowId, Guid documentId)
        => LogOpenDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CloseDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}, Force: {Force}.")]
    private static partial void LogCloseDocumentCalled(ILogger logger, object windowId, Guid documentId, bool force);

    [Conditional("DEBUG")]
    private void LogCloseDocumentCalled(object windowId, Guid documentId, bool force)
        => LogCloseDocumentCalled(this.logger, windowId, documentId, force);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "CloseDocumentAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogCloseDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogCloseDocumentNotFound(object windowId, Guid documentId)
        => LogCloseDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The document was closed for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDocumentClosed(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDocumentClosed(object windowId, Guid documentId)
        => LogDocumentClosed(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "DetachDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDetachDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDetachDocumentCalled(object windowId, Guid documentId)
        => LogDetachDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The document was detached for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDetached(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDetached(object windowId, Guid documentId)
        => LogDetached(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "DetachDocumentAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDetachDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogDetachDocumentNotFound(object windowId, Guid documentId)
        => LogDetachDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachDocumentAsync was called for TargetWindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogAttachDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogAttachDocumentCalled(object windowId, Guid documentId)
        => LogAttachDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The document was attached to WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogAttached(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogAttached(object windowId, Guid documentId)
        => LogAttached(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "UpdateMetadataAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogUpdateMetadataCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogUpdateMetadataCalled(object windowId, Guid documentId)
        => LogUpdateMetadataCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "UpdateMetadataAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogUpdateMetadataFailed(ILogger logger, object windowId, Guid documentId);

    private void LogUpdateMetadataFailed(object windowId, Guid documentId)
        => LogUpdateMetadataFailed(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "UpdateMetadataAsync updated the metadata for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogUpdateMetadataSuccess(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogUpdateMetadataSuccess(object windowId, Guid documentId)
        => LogUpdateMetadataSuccess(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SelectDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogSelectDocumentCalled(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogSelectDocumentCalled(object windowId, Guid documentId)
        => LogSelectDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "SelectDocumentAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogSelectDocumentNotFound(ILogger logger, object windowId, Guid documentId);

    private void LogSelectDocumentNotFound(object windowId, Guid documentId)
        => LogSelectDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "DocumentActivated was invoked for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDocumentActivatedInvoked(ILogger logger, object windowId, Guid documentId);

    [Conditional("DEBUG")]
    private void LogDocumentActivatedInvoked(object windowId, Guid documentId)
        => LogDocumentActivatedInvoked(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OpenDocumentAsync was aborted for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDocumentOpenAborted(ILogger logger, object windowId, Guid documentId);

    private void LogDocumentOpenAborted(object windowId, Guid documentId)
        => LogDocumentOpenAborted(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "AttachDocumentAsync was aborted for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogAttachDocumentAborted(ILogger logger, object windowId, Guid documentId);

    private void LogAttachDocumentAborted(object windowId, Guid documentId)
        => LogAttachDocumentAborted(this.logger, windowId, documentId);
}
