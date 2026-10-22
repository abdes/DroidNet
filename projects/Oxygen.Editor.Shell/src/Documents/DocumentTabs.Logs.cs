// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Documents;

public partial class DocumentTabs
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OpenDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogOpenDocumentCalled(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogOpenDocumentCalled(object windowId, DocumentId documentId)
        => LogOpenDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CloseDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}, Force: {Force}.")]
    private static partial void LogCloseDocumentCalled(ILogger logger, object windowId, DocumentId documentId, bool force);

    [Conditional("DEBUG")]
    private void LogCloseDocumentCalled(object windowId, DocumentId documentId, bool force)
        => LogCloseDocumentCalled(this.logger, windowId, documentId, force);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "CloseDocumentAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogCloseDocumentNotFound(ILogger logger, object windowId, DocumentId documentId);

    private void LogCloseDocumentNotFound(object windowId, DocumentId documentId)
        => LogCloseDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The document was closed for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDocumentClosed(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentClosed(object windowId, DocumentId documentId)
        => LogDocumentClosed(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachDocumentAsync was called for TargetWindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogAttachDocumentCalled(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogAttachDocumentCalled(object windowId, DocumentId documentId)
        => LogAttachDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The document was attached to WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogAttached(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogAttached(object windowId, DocumentId documentId)
        => LogAttached(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SelectDocumentAsync was called for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogSelectDocumentCalled(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogSelectDocumentCalled(object windowId, DocumentId documentId)
        => LogSelectDocumentCalled(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "SelectDocumentAsync failed: document not found for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogSelectDocumentNotFound(ILogger logger, object windowId, DocumentId documentId);

    private void LogSelectDocumentNotFound(object windowId, DocumentId documentId)
        => LogSelectDocumentNotFound(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "DocumentActivated was invoked for WindowId: {WindowId}, DocumentId: {DocumentId}.")]
    private static partial void LogDocumentActivatedInvoked(ILogger logger, object windowId, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentActivatedInvoked(object windowId, DocumentId documentId)
        => LogDocumentActivatedInvoked(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OpenDocumentAsync was refused for WindowId: {WindowId}, DocumentId: {DocumentId}: the document already has a tab.")]
    private static partial void LogDocumentOpenAborted(ILogger logger, object windowId, DocumentId documentId);

    private void LogDocumentOpenAborted(object windowId, DocumentId documentId)
        => LogDocumentOpenAborted(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "AttachDocumentAsync was refused for WindowId: {WindowId}, DocumentId: {DocumentId}: the document already has a tab.")]
    private static partial void LogAttachDocumentAborted(ILogger logger, object windowId, DocumentId documentId);

    private void LogAttachDocumentAborted(object windowId, DocumentId documentId)
        => LogAttachDocumentAborted(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Refused to detach the tab of DocumentId: {DocumentId} from WindowId: {WindowId}; it stays where it is.")]
    private static partial void LogDetachRefused(ILogger logger, object windowId, DocumentId documentId);

    private void LogDetachRefused(object windowId, DocumentId documentId)
        => LogDetachRefused(this.logger, windowId, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Detached the tab of DocumentId: {DocumentId} from WindowId: {WindowId}; the document stays open until a window attaches it.")]
    private static partial void LogDetached(ILogger logger, object windowId, DocumentId documentId);

    private void LogDetached(object windowId, DocumentId documentId)
        => LogDetached(this.logger, windowId, documentId);
}
