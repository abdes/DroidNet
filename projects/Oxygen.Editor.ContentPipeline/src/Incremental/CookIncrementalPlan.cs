// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>The saved products to build and verified products reusable by this request.</summary>
/// <param name="Fingerprints">The requested source and generated-product fingerprints.</param>
/// <param name="Reusable">Verified products keyed by source identity.</param>
/// <param name="ValidSharedRoots">Roots whose index and shared resources still match.</param>
internal sealed record CookIncrementalPlan(
    ImmutableDictionary<Uri, string> Fingerprints,
    ImmutableDictionary<Uri, CookProvenance.Product> Reusable,
    ImmutableHashSet<string> ValidSharedRoots)
{
    /// <summary>Gets output descriptors whose shared resources and own bytes remain verified.</summary>
    public ImmutableHashSet<(string rootMount, string virtualPath)> VerifiedOutputs { get; init; } = [];

    /// <summary>Gets verified native paths for filtering jobs reintroduced by scene preparation.</summary>
    public ImmutableHashSet<string> ReusedVirtualPaths => this.Reusable.Values.SelectMany(static product => product.Outputs).Select(static output => output.Asset.VirtualPath).ToImmutableHashSet(StringComparer.Ordinal);

    /// <summary>Gets the concrete reused assets displayed in the operation result.</summary>
    public ImmutableArray<ContentCookedAsset> ReusedAssets => [.. this.Reusable.Values.SelectMany(static product => product.Outputs).Select(static output => output.Asset)];

    /// <summary>Gets diagnostics that still apply to reused products.</summary>
    public ImmutableArray<Oxygen.Managed.Core.Diagnostics.DiagnosticRecord> Diagnostics => [.. this.Reusable.Values.SelectMany(static product => product.Diagnostics).DistinctBy(static diagnostic => diagnostic.DiagnosticId)];

    /// <summary>Gets a value indicating whether every requested product can be reused.</summary>
    public bool IsUpToDate => this.Fingerprints.Keys.All(this.Reusable.ContainsKey);

    /// <summary>Gets the required generated identities, including the native default material.</summary>
    /// <param name="graph">The captured dependency graph.</param>
    /// <returns>The engine recipe closure.</returns>
    public static ImmutableArray<Uri> Builtins(CookDependencyGraph graph)
        => graph.Builtins.Any(ProceduralGeometryDescriptorService.IsGeneratedBasicShape)
            ? [.. graph.Builtins.Append(AssetUris.BuildGeneratedUri("Materials/Default")).Distinct()]
            : graph.Builtins;
}
