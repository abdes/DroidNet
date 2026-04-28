// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Authored asset kinds with standard project-layout creation targets.
/// </summary>
public enum AuthoringAssetKind
{
    /// <summary>
    ///     Scene descriptor assets.
    /// </summary>
    Scene,

    /// <summary>
    ///     Material descriptor assets.
    /// </summary>
    Material,

    /// <summary>
    ///     Geometry assets.
    /// </summary>
    Geometry,

    /// <summary>
    ///     Texture assets.
    /// </summary>
    Texture,

    /// <summary>
    ///     Audio assets.
    /// </summary>
    Audio,

    /// <summary>
    ///     Video assets.
    /// </summary>
    Video,

    /// <summary>
    ///     Script source assets.
    /// </summary>
    Script,

    /// <summary>
    ///     Prefab assets.
    /// </summary>
    Prefab,

    /// <summary>
    ///     Animation assets.
    /// </summary>
    Animation,
}
