// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Presents documents in a TabStrip by wiring <see cref="IDocumentTabSource"/> events to the UI
///     and forwarding user interactions (selection/close) to the service.
/// </summary>
public partial class DocumentTabPresenter
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting the document tab source to close document with Id={DocumentId}.")]
    private static partial void LogDocumentCloseRequested(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentCloseRequested(DocumentId documentId)
        => LogDocumentCloseRequested(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Document close request, Id={DocumentId}, finished with verdict: {Verdict}.")]
    private static partial void LogDocumentCloseVerdict(ILogger logger, DocumentId documentId, bool verdict);

    [Conditional("DEBUG")]
    private void LogDocumentCloseVerdict(DocumentId documentId, bool verdict)
        => LogDocumentCloseVerdict(this.logger, documentId, verdict);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting the document tab source to detach document with Id={DocumentId}.")]
    private static partial void LogDocumentDetachRequested(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentDetachRequested(DocumentId documentId)
        => LogDocumentDetachRequested(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Document with Id={DocumentId} keeps its tab: no document transfers move it to another window.")]
    private static partial void LogDocumentDetachUnavailable(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentDetachUnavailable(DocumentId documentId)
        => LogDocumentDetachUnavailable(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting the document tab source to attach document with Id={DocumentId} to window {WindowId}.")]
    private static partial void LogDocumentAttachRequested(ILogger logger, DocumentId documentId, ulong windowId);

    [Conditional("DEBUG")]
    private void LogDocumentAttachRequested(DocumentId documentId, ulong windowId)
        => LogDocumentAttachRequested(this.logger, documentId, windowId);

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Document with Id={DocumentId} was refused where it was dropped and goes back to window {WindowId}.")]
    private static partial void LogDocumentReturned(ILogger logger, DocumentId documentId, ulong windowId);

    private void LogDocumentReturned(DocumentId documentId, ulong windowId)
        => LogDocumentReturned(this.logger, documentId, windowId);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "The window for torn-out document with Id={DocumentId} could not be created; the document goes back where it came from.")]
    private static partial void LogDocumentWindowFailed(ILogger logger, DocumentId documentId, Exception exception);

    private void LogDocumentWindowFailed(DocumentId documentId, Exception exception)
        => LogDocumentWindowFailed(this.logger, documentId, exception);
}
