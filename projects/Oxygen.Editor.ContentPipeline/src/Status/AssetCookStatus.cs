// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Cook-owned facts for one authored identity, without claiming native runtime readiness.</summary>
/// <param name="AssetUri">The requested authored identity.</param>
/// <param name="Freshness">The saved-input and producer comparison.</param>
/// <param name="HasPublishedOutput">Whether committed provenance contains an output for the source.</param>
/// <param name="HasVerifiedOutput">Whether the prior output and its recorded dependencies remain intact.</param>
/// <param name="Outputs">The committed source-to-cooked identity mapping.</param>
/// <param name="UnsavedDocuments">Unsaved owners in the required saved dependency closure.</param>
/// <param name="Diagnostics">Read, validation or native-availability issues for this status.</param>
public sealed record AssetCookStatus(
    Uri AssetUri,
    AssetCookFreshness Freshness,
    bool HasPublishedOutput,
    bool HasVerifiedOutput,
    ImmutableArray<ContentCookedAsset> Outputs,
    ImmutableArray<CookDocumentState> UnsavedDocuments,
    ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets saved authoring paths in this asset's dependency closure, for live document overlays.</summary>
    public ImmutableArray<string> SourcePaths { get; init; } = [];

    /// <summary>Gets the hash of source bytes used by this status check, for cached source previews.</summary>
    public string? SavedSourceHash { get; init; }

    /// <summary>Gets a value indicating whether an owner has newer unsaved input.</summary>
    public bool HasUnsavedChanges => !this.UnsavedDocuments.IsEmpty;
}
