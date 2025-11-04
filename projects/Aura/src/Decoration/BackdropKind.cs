// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Specifies the backdrop material effect applied to a window's background.
/// </summary>
/// <remarks>
///     Backdrop effects provide visual depth and hierarchy by applying materials like Mica or
///     Acrylic to the window background. These effects integrate with the system theme and can
///     improve perceived performance by reducing visual complexity.
/// </remarks>
public enum BackdropKind
{
    /// <summary>
    ///     No backdrop effect is applied. The window uses a solid color background.
    /// </summary>
    None,

    /// <summary>
    ///     Mica backdrop material. A subtle, dynamic material that adapts to the desktop wallpaper.
    ///     Provides a soft, textured appearance suitable for app content areas.
    /// </summary>
    Mica,

    /// <summary>
    ///     Mica Alt backdrop material. An alternative Mica variant optimized for surfaces that
    ///     require more visual separation from the base layer.
    /// </summary>
    MicaAlt,

    /// <summary>
    ///     Acrylic backdrop material. A semi-transparent material with blur effects. Provides depth
    ///     and context by revealing content behind the window.
    /// </summary>
    Acrylic,
}
