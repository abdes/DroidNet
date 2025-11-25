// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

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
        => layout switch
        {
            SceneViewLayout.OnePane => (1, 1),
            SceneViewLayout.TwoMainLeft => (1, 2),
            SceneViewLayout.TwoMainRight => (1, 2),
            SceneViewLayout.TwoMainTop => (2, 1),
            SceneViewLayout.TwoMainBottom => (2, 1),
            SceneViewLayout.ThreeMainLeft => (2, 2),
            SceneViewLayout.ThreeMainRight => (2, 2),
            SceneViewLayout.ThreeMainTop => (2, 2),
            SceneViewLayout.ThreeMainBottom => (2, 2),
            SceneViewLayout.FourMainLeft => (3, 3),
            SceneViewLayout.FourMainRight => (3, 3),
            SceneViewLayout.FourMainTop => (3, 3),
            SceneViewLayout.FourMainBottom => (3, 3),
            SceneViewLayout.FourQuad => (2, 2),
            _ => (1, 1),
        };

    /// <summary>
    /// Returns a placement description for viewports in the layout. Each placement is (row, col, rowspan, colspan).
    /// The returned list length indicates how many viewports are expected and the order maps to the ViewModel.Viewports collection indices.
    /// </summary>
    public static IReadOnlyList<(int row, int col, int rowspan, int colspan)> GetPlacements(SceneViewLayout layout)
    {
        // We'll use small grids and place spans accordingly.
        return layout switch
        {
            SceneViewLayout.OnePane => new[] { (0, 0, 1, 1) },

            SceneViewLayout.FourQuad => new[]
            {
                (0, 0, 1, 1),
                (0, 1, 1, 1),
                (1, 0, 1, 1),
                (1, 1, 1, 1),
            },

            // Two panes: grid 1x2 or 2x1 - main is always first item
            SceneViewLayout.TwoMainLeft => new[] { (0, 0, 1, 1), (0, 1, 1, 1) },
            SceneViewLayout.TwoMainRight => new[] { (0, 1, 1, 1), (0, 0, 1, 1) },
            SceneViewLayout.TwoMainTop => new[] { (0, 0, 1, 1), (1, 0, 1, 1) },
            SceneViewLayout.TwoMainBottom => new[] { (1, 0, 1, 1), (0, 0, 1, 1) },

            // Three panes: use 2x2 grid: main occupies left full (2x1) or top full (1x2)
            SceneViewLayout.ThreeMainLeft => new[]
            {
                (0, 0, 2, 1), // main
                (0, 1, 1, 1),
                (1, 1, 1, 1),
            },
            SceneViewLayout.ThreeMainRight => new[]
            {
                (0, 1, 2, 1), // main
                (0, 0, 1, 1),
                (1, 0, 1, 1),
            },
            SceneViewLayout.ThreeMainTop => new[]
            {
                (0, 0, 1, 2), // main
                (1, 0, 1, 1),
                (1, 1, 1, 1),
            },
            SceneViewLayout.ThreeMainBottom => new[]
            {
                (1, 0, 1, 2), // main
                (0, 0, 1, 1),
                (0, 1, 1, 1),
            },

            // Four panes where main occupies two thirds and others split the remaining third.
            // Use a 3x3 grid and map spans so main takes 2x3 or 3x2 cells.
            SceneViewLayout.FourMainLeft => new[]
            {
                (0, 0, 3, 2), // main: left two columns (2/3)
                (0, 2, 1, 1),
                (1, 2, 1, 1),
                (2, 2, 1, 1),
            },
            SceneViewLayout.FourMainRight => new[]
            {
                (0, 1, 3, 2), // main: right two columns
                (0, 0, 1, 1),
                (1, 0, 1, 1),
                (2, 0, 1, 1),
            },
            SceneViewLayout.FourMainTop => new[]
            {
                (0, 0, 2, 3), // main: top two rows
                (2, 0, 1, 1),
                (2, 1, 1, 1),
                (2, 2, 1, 1),
            },
            SceneViewLayout.FourMainBottom => new[]
            {
                (1, 0, 2, 3), // main: bottom two rows
                (0, 0, 1, 1),
                (0, 1, 1, 1),
                (0, 2, 1, 1),
            },

            _ => new[] { (0, 0, 1, 1) },
        };
    }
}
