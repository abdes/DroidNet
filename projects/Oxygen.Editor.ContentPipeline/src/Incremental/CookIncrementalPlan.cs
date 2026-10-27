// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>The saved products to build and verified products reusable by this request.</summary>
/// <param name="Fingerprints">The requested source and generated-product fingerprints.</param>
/// <param name="Reusable">Verified products keyed by source identity.</param>
/// <param name="ValidSharedRoots">Roots whose index and shared resources still match.</param>
internal sealed record CookIncrementalPlan(
    ImmutableDictionary<AssetPath, string> Fingerprints,
    ImmutableDictionary<AssetPath, CookProvenance.Product> Reusable,
    ImmutableHashSet<string> ValidSharedRoots)
{
    /// <summary>Gets committed inventory metadata for comparison with candidate outputs; this is not a live read lease.</summary>
    public ImmutableDictionary<string, CookedInventoryReport> PriorInventories { get; init; } = [];

    /// <summary>
    /// Gets the mounts whose every product was retired and that nothing in this cook goes into. They leave the
    /// publication, as if never cooked, rather than being published as empty roots.
    /// </summary>
    public ImmutableHashSet<string> DroppedRoots { get; init; } = ImmutableHashSet<string>.Empty.WithComparer(StringComparer.Ordinal);

    /// <summary>Gets the sources whose prior products leave the cooked content, so they are never reused.</summary>
    public ImmutableHashSet<AssetPath> RemovedSources { get; init; } = [];

    /// <summary>Gets the outputs removed from each inherited root that stays in the publication, by mount.</summary>
    public ImmutableDictionary<string, CookedOutputRemoval> Removals { get; init; } = ImmutableDictionary<string, CookedOutputRemoval>.Empty.WithComparers(StringComparer.Ordinal);

    /// <summary>Gets output descriptors whose shared resources and own bytes remain verified.</summary>
    public ImmutableHashSet<(string RootMount, string VirtualPath)> VerifiedOutputs { get; init; } = [];

    /// <summary>Gets verified native paths for filtering jobs reintroduced by scene preparation.</summary>
    public ImmutableHashSet<string> ReusedVirtualPaths => this.Reusable.Values.SelectMany(static product => product.Outputs).Select(static output => output.Asset.VirtualPath).ToImmutableHashSet(StringComparer.Ordinal);

    /// <summary>Gets the concrete reused assets displayed in the operation result.</summary>
    public ImmutableArray<ContentCookedAsset> ReusedAssets => [.. this.Reusable.Values.SelectMany(static product => product.Outputs).Select(static output => output.Asset)];

    /// <summary>Gets diagnostics that still apply to reused products.</summary>
    public ImmutableArray<Oxygen.Managed.Core.Diagnostics.DiagnosticRecord> Diagnostics => [.. this.Reusable.Values.SelectMany(static product => product.Diagnostics).DistinctBy(static diagnostic => diagnostic.DiagnosticId)];

    /// <summary>Gets the identities of the diagnostics that still apply to reused products.</summary>
    public ImmutableHashSet<Guid> DiagnosticIds => [.. this.Diagnostics.Select(static diagnostic => diagnostic.DiagnosticId)];

    /// <summary>Gets a value indicating whether every requested product can be reused and no output is removed.</summary>
    public bool IsUpToDate => this.Fingerprints.Keys.All(this.Reusable.ContainsKey) && this.Removals.IsEmpty;
}
