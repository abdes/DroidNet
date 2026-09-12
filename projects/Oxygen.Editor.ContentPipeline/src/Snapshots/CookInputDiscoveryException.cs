// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Stops coherent capture when dependency discovery reports invalid input.</summary>
/// <param name="diagnostics">The source-scoped discovery failures.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "Discovery failures must preserve the structured input diagnostics used by cook recovery.")]
public sealed class CookInputDiscoveryException(IEnumerable<DiagnosticRecord> diagnostics) : Exception("Saved input discovery failed.")
{
    /// <summary>Gets the immutable input diagnostics.</summary>
    public ImmutableArray<DiagnosticRecord> Diagnostics { get; } = [.. diagnostics];
}
