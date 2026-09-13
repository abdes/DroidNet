// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Identifies an indexed cooked asset independently of its virtual filename and physical layout.</summary>
/// <param name="RootFolderPath">The normalized physical root containing the index.</param>
/// <param name="DescriptorRelativePath">The descriptor location recorded by the index, relative to that root.</param>
/// <param name="SourceIdentity">The source identity of the cooked container.</param>
/// <param name="AssetKey">The native asset identity recorded by the index.</param>
/// <param name="AssetType">The native asset type recorded by the index.</param>
/// <param name="DescriptorSize">The recorded descriptor length.</param>
/// <param name="DescriptorSha256">The recorded descriptor digest in hexadecimal.</param>
public sealed record CookedAssetMetadata(
    string RootFolderPath,
    string DescriptorRelativePath,
    Guid SourceIdentity,
    AssetKey AssetKey,
    byte AssetType,
    ulong DescriptorSize,
    string DescriptorSha256);
