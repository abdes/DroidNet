// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Presents documents in a TabStrip by wiring <see cref="IDocumentService"/> events to the UI
///     and forwarding user interactions (selection/close) to the service.
/// </summary>
public partial class DocumentTabPresenter
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting the document service to close document with Id={DocumentId}.")]
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
        Message = "Requesting the document service to detach document with Id={DocumentId}.")]
    private static partial void LogDocumentDetachRequested(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDocumentDetachRequested(DocumentId documentId)
        => LogDocumentDetachRequested(this.logger, documentId);
}
