// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
/// Defines the layout configuration for the scene editor viewports.
/// </summary>
public enum SceneViewLayout
{
    /// <summary>A single viewport filling the entire area.</summary>
    OnePane,

    /// <summary>Two viewports arranged vertically (side-by-side).</summary>
    TwoVertical,

    /// <summary>Two viewports arranged horizontally (stacked).</summary>
    TwoHorizontal,

    /// <summary>Four viewports arranged in a 2x2 grid.</summary>
    FourQuad,
}
