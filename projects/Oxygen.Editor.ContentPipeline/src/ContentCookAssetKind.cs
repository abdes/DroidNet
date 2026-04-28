// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Asset kinds used by ContentPipeline contracts.
/// </summary>
public enum ContentCookAssetKind
{
    /// <summary>
    /// Material descriptor or cooked material.
    /// </summary>
    Material,

    /// <summary>
    /// Geometry descriptor or cooked geometry.
    /// </summary>
    Geometry,

    /// <summary>
    /// Scene descriptor or cooked scene.
    /// </summary>
    Scene,

    /// <summary>
    /// Texture descriptor or cooked texture.
    /// </summary>
    Texture,

    /// <summary>
    /// Foreign source asset.
    /// </summary>
    ForeignSource,

    /// <summary>
    /// Unknown or unsupported asset kind.
    /// </summary>
    Unknown,
}
