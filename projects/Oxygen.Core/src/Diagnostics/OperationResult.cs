// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Top-level result for one user-triggered operation.
/// </summary>
public sealed record OperationResult
{
    /// <summary>
    /// Gets the operation correlation identity.
    /// </summary>
    public required Guid OperationId { get; init; }

    /// <summary>
    /// Gets the stable operation kind.
    /// </summary>
    public required string OperationKind { get; init; }

    /// <summary>
    /// Gets the operation status.
    /// </summary>
    public required OperationStatus Status { get; init; }

    /// <summary>
    /// Gets the computed operation severity.
    /// </summary>
    public required DiagnosticSeverity Severity { get; init; }

    /// <summary>
    /// Gets a short user-facing title.
    /// </summary>
    public required string Title { get; init; }

    /// <summary>
    /// Gets actionable user-facing details.
    /// </summary>
    public required string Message { get; init; }

    /// <summary>
    /// Gets the start timestamp, when known.
    /// </summary>
    public DateTimeOffset? StartedAt { get; init; }

    /// <summary>
    /// Gets the completion timestamp, when known.
    /// </summary>
    public DateTimeOffset? CompletedAt { get; init; }

    /// <summary>
    /// Gets the primary affected scope.
    /// </summary>
    public AffectedScope AffectedScope { get; init; } = AffectedScope.Empty;

    /// <summary>
    /// Gets ordered child diagnostics.
    /// </summary>
    public IReadOnlyList<DiagnosticRecord> Diagnostics { get; init; } = [];

    /// <summary>
    /// Gets the optional primary action.
    /// </summary>
    public PrimaryAction? PrimaryAction { get; init; }
}
