// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Converts exceptions into diagnostics at subsystem boundaries.
/// </summary>
public interface IExceptionDiagnosticAdapter
{
    /// <summary>
    /// Converts an exception into a diagnostic record.
    /// </summary>
    /// <param name="exception">The exception to convert.</param>
    /// <param name="operationId">The operation correlation identity.</param>
    /// <param name="domain">The narrowest known failure domain.</param>
    /// <param name="code">The stable diagnostic code.</param>
    /// <param name="message">The user-facing diagnostic message.</param>
    /// <param name="affectedPath">The optional affected path.</param>
    /// <param name="affectedEntity">The optional affected entity.</param>
    /// <returns>A diagnostic record.</returns>
    public DiagnosticRecord ToDiagnostic(
        Exception exception,
        Guid operationId,
        FailureDomain domain,
        string code,
        string message,
        string? affectedPath = null,
        AffectedScope? affectedEntity = null);
}
