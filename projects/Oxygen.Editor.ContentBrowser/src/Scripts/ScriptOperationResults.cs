// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Publishes script authoring operation results; the Output panel shows every published result.
/// </summary>
public static class ScriptOperationResults
{
    /// <summary>Publishes a failed script operation.</summary>
    /// <param name="publisher">The operation-result publisher.</param>
    /// <param name="operationKind">The <see cref="ScriptOperationKinds"/> value.</param>
    /// <param name="code">The <see cref="ScriptDiagnosticCodes"/> value.</param>
    /// <param name="title">The short title.</param>
    /// <param name="message">What failed, in the user's terms.</param>
    /// <param name="path">The affected file, if any.</param>
    /// <param name="exception">The exception, if any.</param>
    public static void PublishFailure(
        IOperationResultPublisher publisher,
        string operationKind,
        string code,
        string title,
        string message,
        string? path = null,
        Exception? exception = null)
    {
        ArgumentNullException.ThrowIfNull(publisher);
        var operationId = Guid.NewGuid();
        publisher.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope { AssetSourcePath = path },
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.ScriptAuthoring,
                    Severity = DiagnosticSeverity.Error,
                    Code = code,
                    Message = message,
                    TechnicalMessage = exception?.Message,
                    ExceptionType = exception?.GetType().FullName,
                    AffectedPath = path,
                },
            ],
        });
    }

    /// <summary>Publishes a completed script operation, so it is recorded in Output.</summary>
    /// <param name="publisher">The operation-result publisher.</param>
    /// <param name="operationKind">The <see cref="ScriptOperationKinds"/> value.</param>
    /// <param name="title">The short title.</param>
    /// <param name="message">What changed.</param>
    /// <param name="path">The affected file or folder, if any.</param>
    public static void PublishSuccess(IOperationResultPublisher publisher, string operationKind, string title, string message, string? path = null)
    {
        ArgumentNullException.ThrowIfNull(publisher);
        publisher.Publish(new OperationResult
        {
            OperationId = Guid.NewGuid(),
            OperationKind = operationKind,
            Status = OperationStatus.Succeeded,
            Severity = DiagnosticSeverity.Info,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope { AssetSourcePath = path },
        });
    }
}
