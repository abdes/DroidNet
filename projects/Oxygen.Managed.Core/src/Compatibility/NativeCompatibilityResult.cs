// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Either an owned, verified artifact set or actionable compatibility failures.</summary>
/// <param name="Artifacts">The verified set; the caller owns its disposal through native work and drain.</param>
/// <param name="Diagnostics">Compatibility failures, without changing the accepted receipt.</param>
public sealed record NativeCompatibilityResult(NativeArtifactLease? Artifacts, ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets a value indicating whether the complete required artifact set was verified.</summary>
    public bool Succeeded => this.Artifacts is not null && this.Diagnostics.IsEmpty;
}
