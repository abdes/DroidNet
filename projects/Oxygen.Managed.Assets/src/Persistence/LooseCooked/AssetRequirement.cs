// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>An asset or a texture of another mount that an asset of the root references.</summary>
/// <param name="AssetIndex">The index of the requiring asset in <see cref="Document.Assets"/>.</param>
/// <param name="Mount">The required mount's name, such as <c>Engine</c>.</param>
/// <param name="IsTexture">Whether the requirement names a texture rather than an asset.</param>
/// <param name="Key">The key of the required asset or texture.</param>
/// <param name="AssetType">The engine asset type of the required asset; zero for a texture.</param>
public sealed record AssetRequirement(uint AssetIndex, string Mount, bool IsTexture, AssetKey Key, byte AssetType);
