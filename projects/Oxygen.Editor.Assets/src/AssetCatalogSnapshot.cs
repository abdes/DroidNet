// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>The asset catalog's records together with the publication they were read from.</summary>
internal sealed class AssetCatalogSnapshot : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="AssetCatalogSnapshot"/> class.</summary>
    /// <param name="records">The records, resolved in native precedence order.</param>
    /// <param name="publication">The publication the cooked records came from; the snapshot takes its own hold.</param>
    /// <param name="publicationError">Why no publication could be read, when that is the case.</param>
    public AssetCatalogSnapshot(IReadOnlyList<AssetRecord> records, ICookPublicationReadLease? publication, string? publicationError = null)
    {
        this.Records = records;
        this.PublicationError = publicationError;
        this.Publication = publication?.Retain();
    }

    /// <summary>Gets the records, resolved in native precedence order.</summary>
    public IReadOnlyList<AssetRecord> Records { get; }

    /// <summary>Gets the publication the cooked records came from, or null when only authored records are available.</summary>
    public ICookPublicationReadLease? Publication { get; }

    /// <summary>Gets why no publication could be read; authored records remain usable.</summary>
    public string? PublicationError { get; }

    /// <inheritdoc />
    public void Dispose() => this.Publication?.Dispose();
}
