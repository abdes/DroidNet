// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Prevents native execution while preserving qualification diagnostics.</summary>
/// <param name="diagnostics">The failures from verification.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "Qualification failures must retain their structured artifact diagnostics.")]
public sealed class ArtifactQualificationException(IEnumerable<DiagnosticRecord> diagnostics)
    : InvalidOperationException("Native operations are unavailable because the installed artifacts are not qualified.")
{
    /// <summary>Gets the exact manifest or artifact failures.</summary>
    public ImmutableArray<DiagnosticRecord> Diagnostics { get; } = [.. diagnostics];
}
