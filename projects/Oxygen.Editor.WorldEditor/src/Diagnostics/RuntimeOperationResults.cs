// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.World.Diagnostics;

/// <summary>
/// Shared helpers for publishing ED-M02 runtime and viewport operation results.
/// </summary>
internal static class RuntimeOperationResults
{
    public static void PublishFailure(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        AffectedScope affectedScope,
        string? affectedPath = null,
        Exception? exception = null,
        string? technicalMessage = null)
        => Publish(
            publisher,
            statusReducer,
            operationKind,
            domain,
            code,
            DiagnosticSeverity.Error,
            false,
            title,
            message,
            affectedScope,
            affectedPath,
            exception,
            technicalMessage);

    public static void PublishWarning(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        AffectedScope affectedScope,
        string? affectedPath = null,
        Exception? exception = null,
        string? technicalMessage = null)
        => Publish(
            publisher,
            statusReducer,
            operationKind,
            domain,
            code,
            DiagnosticSeverity.Warning,
            true,
            title,
            message,
            affectedScope,
            affectedPath,
            exception,
            technicalMessage);

    private static void Publish(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        FailureDomain domain,
        string code,
        DiagnosticSeverity severity,
        bool primaryGoalCompleted,
        string title,
        string message,
        AffectedScope affectedScope,
        string? affectedPath,
        Exception? exception,
        string? technicalMessage)
    {
        ArgumentNullException.ThrowIfNull(publisher);
        ArgumentNullException.ThrowIfNull(statusReducer);

        var operationId = Guid.NewGuid();
        var completedAt = DateTimeOffset.Now;
        var diagnostic = new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = domain,
            Severity = severity,
            Code = code,
            Message = message,
            TechnicalMessage = technicalMessage ?? exception?.Message,
            ExceptionType = exception?.GetType().FullName,
            AffectedPath = affectedPath,
            AffectedEntity = affectedScope,
        };
        var diagnostics = new[] { diagnostic };
        var result = new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = statusReducer.Reduce(primaryGoalCompleted, wasCancelled: false, diagnostics),
            Severity = statusReducer.ComputeSeverity(diagnostics),
            Title = title,
            Message = message,
            StartedAt = completedAt,
            CompletedAt = completedAt,
            AffectedScope = affectedScope,
            Diagnostics = diagnostics,
            PrimaryAction = new PrimaryAction
            {
                ActionId = "open-details",
                Label = "Details",
                Kind = PrimaryActionKind.OpenDetails,
            },
        };

        publisher.Publish(result);
    }
}
