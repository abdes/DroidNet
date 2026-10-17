// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Diagnostics;

/// <summary>
/// Publishes runtime and viewport operation results.
/// </summary>
public static class RuntimeOperationResults
{
    /// <summary>
    /// Publishes a failed operation with one error diagnostic.
    /// </summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="statusReducer">The reducer that derives the result status and severity.</param>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="domain">The failure domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The result title.</param>
    /// <param name="message">The user-facing message.</param>
    /// <param name="affectedScope">The scope the operation affected.</param>
    /// <param name="affectedPath">The affected path, if any.</param>
    /// <param name="exception">The exception behind the diagnostic, if any.</param>
    /// <param name="technicalMessage">The technical message; defaults to the exception message.</param>
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
            primaryGoalCompleted: false,
            title,
            message,
            affectedScope,
            affectedPath,
            exception,
            technicalMessage);

    /// <summary>
    /// Publishes a completed operation with one warning diagnostic.
    /// </summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="statusReducer">The reducer that derives the result status and severity.</param>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="domain">The failure domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The result title.</param>
    /// <param name="message">The user-facing message.</param>
    /// <param name="affectedScope">The scope the operation affected.</param>
    /// <param name="affectedPath">The affected path, if any.</param>
    /// <param name="exception">The exception behind the diagnostic, if any.</param>
    /// <param name="technicalMessage">The technical message; defaults to the exception message.</param>
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
            primaryGoalCompleted: true,
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
