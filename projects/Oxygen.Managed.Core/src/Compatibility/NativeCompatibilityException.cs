// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Prevents native execution while preserving compatibility diagnostics.</summary>
/// <remarks>
/// The message lists each failure, naming the affected file and the remedy, so a log entry
/// alone tells the developer what to rebuild.
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "Compatibility failures must retain their structured artifact diagnostics.")]
public sealed class NativeCompatibilityException : InvalidOperationException
{
    private const string Summary = "Native files are missing or incompatible with this build.";

    /// <summary>Initializes a new instance of the <see cref="NativeCompatibilityException"/> class.</summary>
    /// <param name="diagnostics">The failures from verification.</param>
    public NativeCompatibilityException(IEnumerable<DiagnosticRecord> diagnostics)
        : this([.. diagnostics])
    {
    }

    private NativeCompatibilityException(ImmutableArray<DiagnosticRecord> diagnostics)
        : base(Describe(diagnostics))
    {
        this.Diagnostics = diagnostics;
    }

    /// <summary>Gets the exact receipt or artifact failures.</summary>
    public ImmutableArray<DiagnosticRecord> Diagnostics { get; }

    private static string Describe(ImmutableArray<DiagnosticRecord> diagnostics)
        => diagnostics.IsDefaultOrEmpty
            ? Summary
            : Summary + string.Concat(diagnostics.Select(static diagnostic => Environment.NewLine + "  " + diagnostic.Message));
}
