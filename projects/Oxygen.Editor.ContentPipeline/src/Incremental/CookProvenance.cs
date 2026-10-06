// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>In-memory product facts projected from the selected publication.</summary>
/// <param name="ProjectId">The project that owns the output roots.</param>
/// <param name="Roots">The last verified files in each physical output root.</param>
/// <param name="Products">The source fingerprints responsible for produced assets.</param>
internal sealed partial record CookProvenance(Guid ProjectId, ImmutableArray<CookProvenance.Root> Roots, ImmutableArray<CookProvenance.Product> Products)
{
    /// <summary>Gets accepted native builtin output identities for source dependency resolution.</summary>
    internal ImmutableDictionary<string, Uri> BuiltinOwners => this.Products
        .Where(static product => product.SourceUri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase))
        .SelectMany(static product => product.Outputs.Select(output => KeyValuePair.Create(output.Asset.VirtualPath, product.SourceUri)))
        .ToImmutableDictionary(StringComparer.Ordinal);

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
    public sealed record Product(Uri SourceUri, string Fingerprint, ImmutableArray<Uri> Dependencies, ImmutableArray<Output> Outputs)
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

    internal bool IsValid(ProjectContext project)
    {
        if (this.ProjectId != project.ProjectId || this.Roots.IsDefault || this.Products.IsDefault
            || this.Roots.Any(static root => root is null || root.SourceKey == Guid.Empty
                || root.IndexSha256 is not { Length: 64 } || !root.IndexSha256.All(Uri.IsHexDigit))
            || this.Products.Any(static product => product?.SourceUri is null || !product.SourceUri.IsAbsoluteUri || !ValidSourceFacts(product) || product.Fingerprint is not { Length: 64 } || !product.Fingerprint.All(Uri.IsHexDigit)
                || product.ReuseFingerprint is not { Length: 64 } || !product.ReuseFingerprint.All(Uri.IsHexDigit) || product.Dependencies.IsDefault || product.Outputs.IsDefaultOrEmpty || product.Diagnostics.IsDefault || product.Diagnostics.Any(static diagnostic => diagnostic is null)
                || product.AuxiliaryFiles.IsDefault || product.AuxiliaryFiles.Any(static path => string.IsNullOrWhiteSpace(path)
                    || path.Contains('\\') || path.Contains(':') || path.Split('/').Any(static segment => segment is "" or "." or ".."))
                || product.CookedDependencies.IsDefault || product.CookedDependencies.Any(static dependency => dependency?.AssetUri?.IsAbsoluteUri != true
                    || string.IsNullOrWhiteSpace(dependency.SourceName) || !Path.IsPathFullyQualified(dependency.RootPath)
                    || string.IsNullOrWhiteSpace(dependency.AssetKey) || dependency.ContentFingerprint is not { Length: 64 } || !dependency.ContentFingerprint.All(Uri.IsHexDigit)))
            || this.Roots.Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal).Count != this.Roots.Length
            || this.Products.Select(static product => product.SourceUri).ToHashSet().Count != this.Products.Length)
        {
            return false;
        }

        try
        {
            return this.Roots.All(static root => !string.IsNullOrWhiteSpace(root.Mount)
                    && root.Mount is not ("." or "..") && root.Mount.IndexOfAny(Path.GetInvalidFileNameChars()) < 0)
                && this.Products.SelectMany(static product => product.Outputs).All(output => output?.Asset is not null
                && !string.IsNullOrWhiteSpace(output.Asset.VirtualPath)
                && output.Asset.DescriptorRelativePath is { Length: > 0 } descriptor
                && !Path.IsPathRooted(descriptor) && !descriptor.Contains('\\') && !descriptor.Contains(':')
                && !descriptor.Split('/').Any(static segment => segment is "" or "." or "..")
                && this.Roots.Any(root => string.Equals(root.Mount, output.RootMount, StringComparison.Ordinal)));
        }
        catch (Exception exception) when (exception is InvalidDataException or ArgumentException)
        {
            return false;
        }
    }
}
