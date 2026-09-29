// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Verified product origins and output identities retained between editor sessions.</summary>
/// <param name="Version">The cache format version.</param>
/// <param name="ProjectId">The project that owns the output roots.</param>
/// <param name="Roots">The last verified files in each physical output root.</param>
/// <param name="Products">The source fingerprints responsible for produced assets.</param>
internal sealed record CookProvenance(int Version, Guid ProjectId, ImmutableArray<CookProvenance.Root> Roots, ImmutableArray<CookProvenance.Product> Products)
{
    internal const int CurrentVersion = 3;
    /// <summary>The exact native inventory selected for a physical publication mount.</summary>
    /// <param name="Mount">The physical mount, independent of virtual asset names.</param>
    /// <param name="SourceKey">The native source identity.</param>
    /// <param name="IndexSha256">The digest of the complete native inventory.</param>
    public sealed record Root(string Mount, Guid SourceKey, string IndexSha256)
    {
        /// <summary>Compares inventory identity, independently of observed file health.</summary>
        /// <param name="inventory">The currently protected native inventory.</param>
        /// <returns>Whether it is the committed inventory.</returns>
        public bool Matches(CookedInventoryReport inventory) => this.SourceKey == inventory.SourceKey
            && string.Equals(this.IndexSha256, inventory.IndexSha256, StringComparison.Ordinal);

        /// <summary>Preserves selective descriptor repair while shared data and membership remain valid.</summary>
        /// <param name="inventory">The protected native verification result.</param>
        /// <param name="outputs">Source-associated outputs, including standalone resource descriptors.</param>
        /// <returns>Shared-file validity and the usable physical descriptor paths.</returns>
        public (bool SharedFilesValid, ImmutableHashSet<string> ValidDescriptors) Compare(CookedInventoryReport inventory, IEnumerable<Output> outputs)
        {
            if (!this.Matches(inventory)) { return (false, []); }
            var descriptors = inventory.Assets.Select(static asset => asset.DescriptorPath).ToHashSet(StringComparer.Ordinal);
            descriptors.UnionWith(outputs.Where(output => output.RootMount == this.Mount && output.Asset.DescriptorRelativePath is not null)
                .Select(static output => output.Asset.DescriptorRelativePath!));
            if (inventory.Issues.Any(issue => !descriptors.Contains(issue.RelativePath)
                || issue.Reason is not ("missing" or "size_mismatch" or "digest_mismatch"))) { return (false, []); }
            var damaged = inventory.Issues.Select(static issue => issue.RelativePath).ToHashSet(StringComparer.Ordinal);
            return (true, descriptors.Where(path => inventory.Files.ContainsKey(path) && !damaged.Contains(path)).ToImmutableHashSet(StringComparer.Ordinal));
        }
    }

    /// <summary>A source-owned output and the physical root that contains it.</summary>
    /// <param name="Asset">The authored-to-cooked identity mapping.</param>
    /// <param name="RootMount">The physical output mount.</param>
    public sealed record Output(ContentCookedAsset Asset, string RootMount);

    /// <summary>Saved inputs and dependencies that produced one source's outputs.</summary>
    /// <param name="SourceUri">The authored or engine-generated source identity.</param>
    /// <param name="Fingerprint">The inputs affecting the emitted product.</param>
    /// <param name="Dependencies">The logical dependencies needed by the product.</param>
    /// <param name="Outputs">The produced asset identities.</param>
    public sealed record Product(Uri SourceUri, string Fingerprint, ImmutableArray<Uri> Dependencies, ImmutableArray<Output> Outputs)
    {
        /// <summary>Gets the source state accepted for reuse after producer-owned source metadata is committed.</summary>
        public required string ReuseFingerprint { get; init; }
        /// <summary>Gets the external native identities used when the source was cooked.</summary>
        public ImmutableArray<Snapshots.CookedDependencySnapshot> CookedDependencies { get; init; } = [];

        /// <summary>Gets source discovery needed to reuse an imported product without another native query.</summary>
        public Import.ImportedSourceDependencyState? ImportedSource { get; init; }

        /// <summary>Gets native-reported auxiliary files emitted by this imported source, without duplicating inventory digests.</summary>
        public ImmutableArray<string> AuxiliaryFiles { get; init; } = [];

        /// <summary>Gets warnings that still apply when the unchanged product is reused.</summary>
        public ImmutableArray<Oxygen.Managed.Core.Diagnostics.DiagnosticRecord> Diagnostics { get; init; } = [];
    }
}
