// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Cook-owned facts for one authored identity, without claiming native runtime readiness.</summary>
/// <param name="AssetUri">The requested authored identity.</param>
/// <param name="Freshness">The saved-input and producer comparison.</param>
/// <param name="HasPublishedOutput">Whether committed provenance contains an output for the source.</param>
/// <param name="OutputAvailability">Observed publication availability, independent of payload integrity.</param>
/// <param name="Outputs">The committed source-to-cooked identity mapping.</param>
/// <param name="Diagnostics">Read, validation or native-availability issues for this status.</param>
public sealed record AssetCookStatus(
    AssetPath AssetUri,
    AssetCookFreshness Freshness,
    bool HasPublishedOutput,
    CookedOutputAvailability OutputAvailability,
    ImmutableArray<ContentCookedAsset> Outputs,
    ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets whether the published output is present, without claiming payload integrity.</summary>
    public bool HasAvailableOutput => this.OutputAvailability == CookedOutputAvailability.Present;

    /// <summary>
    /// Gets the open documents with unsaved changes to files in this asset's dependency closure. A status read from the
    /// files has none; the asset feed adds them from <see cref="IUnsavedDocuments"/>.
    /// </summary>
    public ImmutableArray<UnsavedDocument> UnsavedDocuments { get; init; } = [];

    /// <summary>Gets saved authoring paths in this asset's dependency closure, for live document overlays.</summary>
    public ImmutableArray<FileSystemPath> SourcePaths { get; init; } = [];

    /// <summary>Gets the selected generation paths used by this status read, shared across its result batch.</summary>
    public ImmutableDictionary<string, FileSystemPath> OutputRoots { get; init; } = ImmutableDictionary<string, FileSystemPath>.Empty;

    /// <summary>Gets the hash of source bytes used by this status check, for cached source previews.</summary>
    public string? SavedSourceHash { get; init; }

    /// <summary>Gets a value indicating whether an open document has unsaved changes to the asset's inputs.</summary>
    public bool HasUnsavedChanges => !this.UnsavedDocuments.IsEmpty;
}
