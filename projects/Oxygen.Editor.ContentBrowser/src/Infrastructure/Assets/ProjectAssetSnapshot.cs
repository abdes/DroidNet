// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>One catalog result and the publication lifetime used to resolve its physical output.</summary>
public sealed class ProjectAssetSnapshot : IDisposable
{
    internal ProjectAssetSnapshot(IReadOnlyList<AssetRecord> records, CookPublicationReadLease? publication, string? publicationError = null)
    {
        this.Records = records;
        this.PublicationError = publicationError;
        this.Publication = publication?.Retain();
    }

    /// <summary>Gets records resolved from the captured source order.</summary>
    public IReadOnlyList<AssetRecord> Records { get; }

    /// <summary>Gets borrowed publication ownership, or null when only authoring is available.</summary>
    public CookPublicationReadLease? Publication { get; }

    /// <summary>Gets the admission failure when cooked metadata is unavailable; authored records remain usable.</summary>
    public string? PublicationError { get; }

    /// <inheritdoc />
    public void Dispose() => this.Publication?.Dispose();
}
