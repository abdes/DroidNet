// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>In-memory product facts projected from the selected publication.</summary>
/// <param name="ProjectId">The project that owns the output roots.</param>
/// <param name="Roots">The last verified files in each physical output root.</param>
/// <param name="Products">The source fingerprints responsible for produced assets.</param>
internal sealed partial record CookProvenance(Guid ProjectId, ImmutableArray<CookProvenance.Root> Roots, ImmutableArray<CookProvenance.Product> Products)
{
    /// <summary>Gets the identity of every asset the publication's sources produced, as the import reports named them.</summary>
    internal IEnumerable<AssetPath> ProducedAssets => this.Products.SelectMany(static product => product.Outputs)
        .Select(static output => output.Asset.CookedAssetUri).Distinct();

    internal bool IsValid(ProjectContext project)
    {
        if (this.ProjectId != project.ProjectId || this.Roots.IsDefault || this.Products.IsDefault
            || !this.Roots.All(IsValidRoot) || !this.Products.All(IsValidProduct))
        {
            return false;
        }

        var mounts = this.Roots.Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal);
        var sources = this.Products.Select(static product => product.SourceUri).ToHashSet();
        if (mounts.Count != this.Roots.Length || sources.Count != this.Products.Length)
        {
            return false;
        }

        try
        {
            return this.Products.SelectMany(static product => product.Outputs).All(output => IsValidOutput(output, mounts));
        }
        catch (Exception exception) when (exception is InvalidDataException or ArgumentException)
        {
            return false;
        }
    }

    private static bool IsValidRoot(Root? root)
        => root is not null
            && root.SourceKey != Guid.Empty
            && IsSha256(root.IndexSha256)
            && !string.IsNullOrWhiteSpace(root.Mount)
            && root.Mount is not ("." or "..")
            && root.Mount.IndexOfAny(Path.GetInvalidFileNameChars()) < 0;

    private static bool IsValidProduct(Product? product)
        => product?.SourceUri is not null
            && ValidSourceFacts(product)
            && IsSha256(product.Fingerprint)
            && IsSha256(product.ReuseFingerprint)
            && !product.Dependencies.IsDefault
            && !product.Outputs.IsDefaultOrEmpty
            && !product.Diagnostics.IsDefault && product.Diagnostics.All(static diagnostic => diagnostic is not null)
            && !product.AuxiliaryFiles.IsDefault && product.AuxiliaryFiles.All(IsContainedRelativePath)
            && !product.CookedDependencies.IsDefault && product.CookedDependencies.All(IsValidCookedDependency);

    private static bool IsValidCookedDependency(Snapshots.CookedDependencySnapshot? dependency)
        => dependency?.AssetUri is not null
            && !string.IsNullOrWhiteSpace(dependency.SourceName)
            && Path.IsPathFullyQualified(dependency.RootPath)
            && !string.IsNullOrWhiteSpace(dependency.AssetKey)
            && IsSha256(dependency.ContentFingerprint);

    // An output names a cooked asset whose descriptor lies inside one of this publication's roots.
    private static bool IsValidOutput(Output? output, HashSet<string> mounts)
        => output?.Asset is { } asset
            && !string.IsNullOrWhiteSpace(asset.VirtualPath)
            && IsContainedRelativePath(asset.DescriptorRelativePath)
            && mounts.Contains(output.RootMount);

    private static bool IsSha256(string? digest)
        => digest is { Length: 64 } && digest.All(Uri.IsHexDigit);

    // A '/'-separated path that cannot leave the folder it is relative to.
    private static bool IsContainedRelativePath(string? path)
        => !string.IsNullOrWhiteSpace(path)
            && !Path.IsPathRooted(path)
            && !path.Contains('\\', StringComparison.Ordinal)
            && !path.Contains(':', StringComparison.Ordinal)
            && path.Split('/').All(static segment => segment is not ("" or "." or ".."));

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
            if (!this.Matches(inventory))
            {
                return (false, []);
            }

            var descriptors = inventory.Assets.Select(static asset => asset.DescriptorPath).ToHashSet(StringComparer.Ordinal);
            descriptors.UnionWith(outputs.Where(output => string.Equals(output.RootMount, this.Mount, StringComparison.Ordinal) && output.Asset.DescriptorRelativePath is not null)
                .Select(static output => output.Asset.DescriptorRelativePath!));
            if (inventory.Issues.Any(issue => !descriptors.Contains(issue.RelativePath)
                || issue.Reason is not ("missing" or "size_mismatch" or "digest_mismatch")))
            {
                return (false, []);
            }

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
    public sealed record Product(AssetPath SourceUri, string Fingerprint, ImmutableArray<AssetPath> Dependencies, ImmutableArray<Output> Outputs)
    {
        /// <summary>Gets the authored owner and recipe identity accepted by this publication.</summary>
        public ContentCookInput? SourceInput { get; init; }

        /// <summary>Gets the source closure accepted for this product, independent of the last partial cook.</summary>
        public ImmutableArray<Snapshots.CookSnapshotInput> SourceFiles { get; init; } = [];

        /// <summary>Gets native-declared outputs used to distinguish internal references.</summary>
        public ImmutableArray<Import.NativeLogicalDependency> DeclaredOutputs { get; init; } = [];

        /// <summary>Gets native logical references, reused by passive status without parsing source.</summary>
        public ImmutableArray<Import.NativeLogicalDependency> NativeReferences { get; init; } = [];

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
