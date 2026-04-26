// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Default implementation of <see cref="IStatusReducer"/>.
/// </summary>
public sealed class OperationStatusReducer : IStatusReducer
{
    /// <inheritdoc/>
    public OperationStatus Reduce(
        bool primaryGoalCompleted,
        bool wasCancelled,
        IReadOnlyCollection<DiagnosticRecord> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);

        if (wasCancelled)
        {
            return OperationStatus.Cancelled;
        }

        if (!primaryGoalCompleted)
        {
            return OperationStatus.Failed;
        }

        var severity = this.ComputeSeverity(diagnostics);
        return severity switch
        {
            DiagnosticSeverity.Fatal or DiagnosticSeverity.Error => OperationStatus.PartiallySucceeded,
            DiagnosticSeverity.Warning => OperationStatus.SucceededWithWarnings,
            _ => OperationStatus.Succeeded,
        };
    }

    /// <inheritdoc/>
    public DiagnosticSeverity ComputeSeverity(IReadOnlyCollection<DiagnosticRecord> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);

        var severity = DiagnosticSeverity.Info;
        foreach (var diagnostic in diagnostics)
        {
            if (diagnostic.Severity > severity)
            {
                severity = diagnostic.Severity;
            }
        }

        return severity;
    }
}
