// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Controls;

/// <summary>Presents an <see cref="OverrideState"/>, for the override bar and reset button of rows and item cards.</summary>
public static class OverrideStates
{
    private const double PartialOpacity = 0.5;

    /// <summary>Gets the override bar's opacity: none, half or full strength.</summary>
    /// <param name="state">How many edited objects override the value.</param>
    /// <returns>The opacity.</returns>
    public static double BarOpacity(OverrideState state)
        => state switch
        {
            OverrideState.Full => 1,
            OverrideState.Partial => PartialOpacity,
            _ => 0,
        };

    /// <summary>Gets whether a reset button shows: only while some edited object overrides the value.</summary>
    /// <param name="state">How many edited objects override the value.</param>
    /// <returns>The reset button's visibility.</returns>
    public static Visibility ResetVisibility(OverrideState state)
        => state == OverrideState.None ? Visibility.Collapsed : Visibility.Visible;
}
