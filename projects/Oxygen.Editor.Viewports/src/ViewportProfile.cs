// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Viewports;

/// <summary>What a viewport is for, which decides the overlay elements it shows (model editor 5.2).</summary>
public enum ViewportProfile
{
    /// <summary>A scene's viewport: every editing overlay, the transform tools, layouts and maximize.</summary>
    Editing,

    /// <summary>
    /// A viewport that looks at content it does not edit, such as a model: one view, its camera, view mode, show options
    /// and framing, without the transform tools, layouts, maximize or camera preview.
    /// </summary>
    Viewing,
}
