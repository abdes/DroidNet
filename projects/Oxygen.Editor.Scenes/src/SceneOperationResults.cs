// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes;

/// <summary>
/// Shared helpers for publishing scene authoring operation results.
/// </summary>
public static class SceneOperationResults
{
    /// <summary>Publishes a failed operation with one error diagnostic.</summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="statusReducer">The reducer that derives the result status and severity.</param>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="domain">The failure domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The result title.</param>
    /// <param name="message">The user-facing message.</param>
    /// <param name="affectedScope">The scope the operation affected.</param>
    /// <param name="exception">The exception behind the diagnostic, if any.</param>
    /// <param name="technicalMessage">The technical message; defaults to the exception message.</param>
    /// <returns>The published operation id.</returns>
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

    /// <summary>Publishes a completed operation with one warning diagnostic.</summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="statusReducer">The reducer that derives the result status and severity.</param>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="domain">The failure domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The result title.</param>
    /// <param name="message">The user-facing message.</param>
    /// <param name="affectedScope">The scope the operation affected.</param>
    /// <param name="exception">The exception behind the diagnostic, if any.</param>
    /// <param name="technicalMessage">The technical message; defaults to the exception message.</param>
    /// <returns>The published operation id.</returns>
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

    /// <summary>
    /// Publishes what the user should know about an edit's result: its first error when it failed, else its first
    /// warning, such as changes an unpack dropped; nothing for an edit that applied cleanly.
    /// </summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="statusReducer">The reducer that derives the result status and severity.</param>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="failureTitle">The result title when the edit failed.</param>
    /// <param name="warningTitle">The result title when the edit applied with a warning.</param>
    /// <param name="result">The edit's result.</param>
    /// <param name="affectedScope">The scope the operation affected.</param>
    public static void PublishEditResult(
        IOperationResultPublisher publisher,
        IStatusReducer statusReducer,
        string operationKind,
        string failureTitle,
        string warningTitle,
        SceneEditResult result,
        AffectedScope affectedScope)
    {
        ArgumentNullException.ThrowIfNull(result);
        if (!result.Succeeded && result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError) is { } error)
        {
            _ = PublishFailure(publisher, statusReducer, operationKind, FailureDomain.SceneAuthoring, error.Code, failureTitle, error.Message, affectedScope);
        }
        else if (result.Succeeded && result.Diagnostics.FirstOrDefault(static diagnostic => !diagnostic.IsError) is { } warning)
        {
            _ = PublishWarning(publisher, statusReducer, operationKind, FailureDomain.SceneAuthoring, warning.Code, warningTitle, warning.Message, affectedScope);
        }
    }

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
