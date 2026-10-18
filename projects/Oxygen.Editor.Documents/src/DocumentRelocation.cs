// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>What a relocation did to an open document's file.</summary>
/// <param name="File">The document's file after the relocation; the same file when only its references moved.</param>
/// <param name="Version">The file's version after the relocation, whether it rewrote the file or not.</param>
/// <param name="MapReference">
/// Maps an authored reference, a full asset URI or a virtual path in any of the forms a document writes, to its new
/// value in the same form; it returns <see langword="null"/> when the referenced asset did not move. A folder that moved
/// moves every reference inside it.
/// </param>
public sealed record DocumentRelocation(DocumentFile File, FileVersion Version, Func<string, string?> MapReference)
{
    /// <summary>Maps an asset to where it moved.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>Its new identity, or <see langword="null"/> when the asset did not move.</returns>
    public AssetPath? MapAsset(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return this.MapReference(asset.ToUriString()) is { } mapped && AssetPath.TryParseUri(mapped, out var moved) ? moved : null;
    }
}
