// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.World.Diagnostics;

/// <summary>
/// Shared helpers for publishing scene authoring operation results.
/// </summary>
internal static class SceneOperationResults
{
    public static Guid PublishFailure(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        AffectedScope affectedScope,
        Exception? exception = null,
        string? technicalMessage = null)
        => Publish(
            publisher,
            statusReducer,
            operationKind,
            domain,
            code,
            DiagnosticSeverity.Error,
            primaryGoalCompleted: false,
            title,
            message,
            affectedScope,
            exception,
            technicalMessage);

    public static Guid PublishWarning(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        AffectedScope affectedScope,
        Exception? exception = null,
        string? technicalMessage = null)
        => Publish(
            publisher,
            statusReducer,
            operationKind,
            domain,
            code,
            DiagnosticSeverity.Warning,
            primaryGoalCompleted: true,
            title,
            message,
            affectedScope,
            exception,
            technicalMessage);

    private static Guid Publish(
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
        return operationId;
    }
}
