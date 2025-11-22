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

/// <summary>
/// Helper methods for working with scene layouts.
/// </summary>
public static class SceneLayoutHelpers
{
    /// <summary>
    /// Gets the grid dimensions (rows, columns) for the specified layout.
    /// </summary>
    /// <param name="layout">The scene view layout.</param>
    /// <returns>A tuple containing the number of rows and columns.</returns>
    public static (int rows, int cols) GetGridDimensions(SceneViewLayout layout)
    {
        return layout switch
        {
            SceneViewLayout.OnePane => (1, 1),
            SceneViewLayout.TwoVertical => (1, 2),
            SceneViewLayout.TwoHorizontal => (2, 1),
            SceneViewLayout.FourQuad => (2, 2),
            _ => (1, 1),
        };
    }
}
