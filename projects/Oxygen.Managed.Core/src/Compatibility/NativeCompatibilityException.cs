// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Prevents native execution while preserving compatibility diagnostics.</summary>
/// <param name="diagnostics">The failures from verification.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "Compatibility failures must retain their structured artifact diagnostics.")]
public sealed class NativeCompatibilityException(IEnumerable<DiagnosticRecord> diagnostics)
    : InvalidOperationException("Native files are missing or incompatible with this build.")
{
    /// <summary>Gets the exact receipt or artifact failures.</summary>
    public ImmutableArray<DiagnosticRecord> Diagnostics { get; } = [.. diagnostics];
}
