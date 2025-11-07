// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Drag;

/// <summary>
///     Configuration constants for drag operations in TabStrip controls.
/// </summary>
public static class DragThresholds
{
    /// <summary>
    ///     The minimum distance in pixels that the pointer must move after initial press
    ///     to initiate a drag operation. This prevents accidental drags from simple clicks.
    /// </summary>
    public const double DragInitiationThreshold = 5.0;

    /// <summary>
    ///     The distance in pixels that the pointer must move outside the TabStrip bounds
    ///     to trigger a transition from Reorder mode to TearOut mode.
    /// </summary>
    public const double TearOutThreshold = 20.0;

    /// <summary>
    ///     The fraction of a tab item's width that the pointer must cross to trigger
    ///     a swap with an adjacent item during reordering.
    /// </summary>
    public const double SwapThreshold = 0.5;
}
