// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Reduces operation state and diagnostics into a public operation status.
/// </summary>
public interface IStatusReducer
{
    /// <summary>
    /// Computes the public operation status.
    /// </summary>
    /// <param name="primaryGoalCompleted">A value indicating whether the operation primary goal completed.</param>
    /// <param name="wasCancelled">A value indicating whether the operation was cancelled.</param>
    /// <param name="diagnostics">The operation diagnostics.</param>
    /// <returns>The reduced operation status.</returns>
    public OperationStatus Reduce(
        bool primaryGoalCompleted,
        bool wasCancelled,
        IReadOnlyCollection<DiagnosticRecord> diagnostics);

    /// <summary>
    /// Computes the maximum severity from diagnostics.
    /// </summary>
    /// <param name="diagnostics">The operation diagnostics.</param>
    /// <returns>The maximum diagnostic severity, or <see cref="DiagnosticSeverity.Info"/> when no diagnostics exist.</returns>
    public DiagnosticSeverity ComputeSeverity(IReadOnlyCollection<DiagnosticRecord> diagnostics);
}
