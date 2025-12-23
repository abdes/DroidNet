// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Material alpha blending semantics.
/// </summary>
public enum MaterialAlphaMode
{
    /// <summary>
    /// Fully opaque material.
    /// </summary>
    Opaque = 0,

    /// <summary>
    /// Alpha-tested (cutout) material.
    /// </summary>
    Mask = 1,

    /// <summary>
    /// Alpha blended (transparent) material.
    /// </summary>
    Blend = 2,
}
