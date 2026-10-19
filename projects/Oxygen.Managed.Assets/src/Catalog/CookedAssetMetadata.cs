// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>
/// Identifies a cooked asset, or a cooked texture, in its container (a loose cooked root or a pak) independently of its
/// virtual filename and physical layout.
/// </summary>
/// <remarks>
/// A cooked texture is a resource, not an asset: its type is <see cref="AssetType"/> 0, and its key is the one the engine
/// derives from its virtual path (<see cref="AssetKey.FromVirtualPath"/>), which other containers' descriptors name.
/// </remarks>
/// <param name="ContainerPath">The normalized physical container: a loose cooked root folder or a pak file.</param>
/// <param name="DescriptorRelativePath">The descriptor file a loose index records, relative to its root; null in a pak, which holds descriptors inside the file.</param>
/// <param name="SourceIdentity">The source identity of the cooked container.</param>
/// <param name="AssetKey">The native asset identity recorded by the container.</param>
/// <param name="AssetType">The native asset type recorded by the container; 0 for a texture.</param>
/// <param name="DescriptorSize">The recorded descriptor length.</param>
/// <param name="DescriptorSha256">The descriptor digest in hexadecimal a loose index records; null in a pak, whose integrity the pak CRC covers.</param>
public sealed record CookedAssetMetadata(
    FileSystemPath ContainerPath,
    string? DescriptorRelativePath,
    Guid SourceIdentity,
    AssetKey AssetKey,
    byte AssetType,
    ulong DescriptorSize,
    string? DescriptorSha256)
{
    /// <summary>Gets a value indicating whether the asset is inside a pak, which has no descriptor file to read or edit.</summary>
    public bool IsPackaged => this.DescriptorRelativePath is null;

    /// <summary>Gets the native virtual path declared by this representation, including aliases of a shared asset key.</summary>
    public AssetPath? VirtualPath { get; init; }

    /// <summary>Gets the texture-table record of a cooked texture, or null for an asset or an unreadable texture.</summary>
    public TextureResourceRecord? Texture { get; init; }

    /// <summary>Gets the opaque native reference-block locator and counts.</summary>
    public AssetReferenceTable References { get; init; }
}
