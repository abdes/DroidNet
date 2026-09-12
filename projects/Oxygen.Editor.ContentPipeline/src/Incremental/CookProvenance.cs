// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Verified product origins and output identities retained between editor sessions.</summary>
/// <param name="Version">The cache format version.</param>
/// <param name="ProjectId">The project that owns the output roots.</param>
/// <param name="Roots">The last verified files in each physical output root.</param>
/// <param name="Products">The source fingerprints responsible for produced assets.</param>
internal sealed record CookProvenance(int Version, Guid ProjectId, ImmutableArray<CookProvenance.Root> Roots, ImmutableArray<CookProvenance.Product> Products)
{
    /// <summary>A file's complete content identity within a cooked root.</summary>
    /// <param name="RelativePath">The root-relative physical path.</param>
    /// <param name="Size">The byte length.</param>
    /// <param name="Sha256">The content hash.</param>
    public sealed record FileProof(string RelativePath, long Size, string Sha256);

    /// <summary>A native index entry and its descriptor bytes.</summary>
    /// <param name="Entry">The inspected asset identity and descriptor location.</param>
    /// <param name="File">The descriptor proof.</param>
    public sealed record IndexedAsset(CookedAssetEntry Entry, FileProof File);

    /// <summary>The physical root, shared resource/index files and individual descriptors.</summary>
    /// <param name="Mount">The physical publication mount, independent of an asset's virtual namespace.</param>
    /// <param name="SharedFiles">Index and shared resource identities.</param>
    /// <param name="Assets">The indexed descriptor identities.</param>
    public sealed record Root(string Mount, ImmutableArray<FileProof> SharedFiles, ImmutableArray<IndexedAsset> Assets);

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
        /// <summary>Gets warnings that still apply when the unchanged product is reused.</summary>
        public ImmutableArray<Oxygen.Managed.Core.Diagnostics.DiagnosticRecord> Diagnostics { get; init; } = [];
    }
}
