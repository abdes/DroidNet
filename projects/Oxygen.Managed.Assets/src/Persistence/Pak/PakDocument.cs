// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.Pak;

/// <summary>The browsable metadata of one pak file.</summary>
/// <param name="ContentVersion">The content version recorded in the pak header.</param>
/// <param name="SourceIdentity">The pak's source identity.</param>
/// <param name="CatalogDigest">The embedded catalog's SHA-256 digest in hexadecimal, which changes with any asset's content.</param>
/// <param name="Assets">The assets its browse index names, in browse-index order.</param>
public sealed record PakDocument(
    ushort ContentVersion,
    Guid SourceIdentity,
    string CatalogDigest,
    IReadOnlyList<PakAssetEntry> Assets)
{
    /// <summary>Gets the textures its browse index names, in browse-index order.</summary>
    public IReadOnlyList<PakTextureEntry> Textures { get; init; } = [];
}
