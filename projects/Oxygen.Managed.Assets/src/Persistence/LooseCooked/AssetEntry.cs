// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

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

    /// <summary>Gets the virtual path of the source that produced the asset, such as
    /// <c>/Content/Materials/Red.omat.json</c>; a re-cook by another source is refused.</summary>
    public string? SourcePath { get; init; }
}
