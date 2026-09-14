// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Resolves embedded native keys without changing their meaning to a virtual-path reference.</summary>
/// <param name="ProjectInputs">Project sources that produce the exact required native identities.</param>
/// <param name="Libraries">The library versions required by this consuming asset.</param>
/// <param name="Diagnostics">Missing or unavailable identity information for this consumer.</param>
public sealed record CookReferenceExpansion(
    ImmutableArray<ContentCookInput> ProjectInputs,
    ImmutableArray<CookedDependencySnapshot> Libraries,
    ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets exact imported outputs required from the captured project sources.</summary>
    public ImmutableArray<Uri> ImportedOutputs { get; init; } = [];
}
