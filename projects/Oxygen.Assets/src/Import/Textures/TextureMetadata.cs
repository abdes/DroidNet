// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Textures;

/// <summary>
/// Source metadata for a texture asset.
/// </summary>
public sealed record TextureMetadata(
    string Schema,
    string Name,
    string Source,
    bool Srgb);
