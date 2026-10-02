// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

public sealed record AssetEntry(
    AssetKey AssetKey,
    string DescriptorRelativePath,
    string? VirtualPath,
    byte AssetType,
    ulong DescriptorSize,
    System.ReadOnlyMemory<byte> DescriptorSha256)
{
    /// <summary>Gets the opaque native reference-block locator.</summary>
    public AssetReferenceTable References { get; init; }
}
