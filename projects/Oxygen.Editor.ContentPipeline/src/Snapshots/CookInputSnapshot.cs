// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Identifies an immutable private saved-input set retained through cook and publication.</summary>
/// <param name="Operation">The owning project operation.</param>
/// <param name="InputRoot">The private directory read by native jobs.</param>
/// <param name="BuildFingerprint">The compatible artifact/schema identity.</param>
/// <param name="InputIdentity">The content identity independent of local paths and operation IDs.</param>
/// <param name="Inputs">The original identities, paths, and captured hashes.</param>
/// <param name="Documents">The saved revisions of participating open documents.</param>
public sealed record CookInputSnapshot(
    ContentCookOperation Operation,
    string InputRoot,
    string BuildFingerprint,
    string InputIdentity,
    ImmutableArray<CookSnapshotInput> Inputs,
    ImmutableArray<CookDocumentState> Documents)
{
    /// <summary>Gets foreign cooked inputs held alongside the private authoring snapshot.</summary>
    public ImmutableArray<CookedDependencySnapshot> CookedDependencies { get; init; } = [];
}
