// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Textures;

/// <summary>How a single image lays out the six faces of a cube texture.</summary>
public enum CubeLayout
{
    /// <summary>A 2:1 equirectangular (latitude-longitude) panorama, resampled to faces.</summary>
    Panorama,

    /// <summary>Six faces side by side, 6:1.</summary>
    HorizontalStrip,

    /// <summary>Six faces stacked, 1:6.</summary>
    VerticalStrip,

    /// <summary>A 4:3 horizontal cross.</summary>
    HorizontalCross,

    /// <summary>A 3:4 vertical cross.</summary>
    VerticalCross,

    /// <summary>Six square images of the same size, one per face, named with a face suffix such as <c>_px</c>.</summary>
    SixFaces,
}
