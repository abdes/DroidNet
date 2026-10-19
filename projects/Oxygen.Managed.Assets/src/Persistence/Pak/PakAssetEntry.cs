// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.Pak;

/// <summary>One asset a pak's browse index names, joined with its directory entry.</summary>
/// <param name="AssetKey">The native asset identity.</param>
/// <param name="AssetType">The native asset type.</param>
/// <param name="VirtualPath">The canonical virtual path the browse index records.</param>
/// <param name="DescriptorOffset">The absolute offset of the descriptor in the pak.</param>
/// <param name="DescriptorSize">The descriptor length in bytes.</param>
public sealed record PakAssetEntry(
    AssetKey AssetKey,
    byte AssetType,
    string VirtualPath,
    ulong DescriptorOffset,
    uint DescriptorSize)
{
    /// <summary>Gets the opaque native reference-block locator.</summary>
    public AssetReferenceTable References { get; init; }
}
