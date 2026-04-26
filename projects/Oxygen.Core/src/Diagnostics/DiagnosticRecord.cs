// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Structured diagnostic detail for a user-triggered operation.
/// </summary>
public sealed record DiagnosticRecord
{
    /// <summary>
    /// Gets the per-instance diagnostic identity.
    /// </summary>
    public Guid DiagnosticId { get; init; } = Guid.NewGuid();

    /// <summary>
    /// Gets the operation correlation identity.
    /// </summary>
    public required Guid OperationId { get; init; }

    /// <summary>
    /// Gets the failure domain.
    /// </summary>
    public required FailureDomain Domain { get; init; }

    /// <summary>
    /// Gets the diagnostic severity.
    /// </summary>
    public required DiagnosticSeverity Severity { get; init; }

    /// <summary>
    /// Gets the stable diagnostic code.
    /// </summary>
    public required string Code { get; init; }

    /// <summary>
    /// Gets the user-readable diagnostic message.
    /// </summary>
    public required string Message { get; init; }

    /// <summary>
    /// Gets optional technical details.
    /// </summary>
    public string? TechnicalMessage { get; init; }

    /// <summary>
    /// Gets the optional exception type.
    /// </summary>
    public string? ExceptionType { get; init; }

    /// <summary>
    /// Gets the optional affected filesystem path.
    /// </summary>
    public string? AffectedPath { get; init; }

    /// <summary>
    /// Gets the optional affected virtual path.
    /// </summary>
    public string? AffectedVirtualPath { get; init; }

    /// <summary>
    /// Gets the optional affected entity.
    /// </summary>
    public AffectedScope? AffectedEntity { get; init; }

    /// <summary>
    /// Gets the optional suggested action.
    /// </summary>
    public PrimaryAction? SuggestedAction { get; init; }
}
