// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Default implementation of <see cref="IExceptionDiagnosticAdapter"/>.
/// </summary>
public sealed class ExceptionDiagnosticAdapter : IExceptionDiagnosticAdapter
{
    /// <inheritdoc/>
    public DiagnosticRecord ToDiagnostic(
        Exception exception,
        Guid operationId,
        FailureDomain domain,
        string code,
        string message,
        string? affectedPath = null,
        AffectedScope? affectedEntity = null)
    {
        ArgumentNullException.ThrowIfNull(exception);
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);

        return new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = domain,
            Severity = exception is OperationCanceledException
                ? DiagnosticSeverity.Info
                : DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            TechnicalMessage = exception.Message,
            ExceptionType = exception.GetType().FullName,
            AffectedPath = affectedPath,
            AffectedEntity = affectedEntity,
        };
    }
}
