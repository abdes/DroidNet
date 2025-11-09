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
    ///     The minimum distance in pixels that the pointer must move after initial press to
    ///     initiate a drag operation. This prevents accidental drags from simple clicks.
    /// </summary>
    public const double InitiationThreshold = 5.0;

    /// <summary>
    ///     The distance in pixels that the pointer must move outside the TabStrip bounds to trigger
    ///     a transition from Reorder mode to TearOut mode.
    /// </summary>
    public const double TearOutThreshold = 5.0;

    /// <summary>
    ///     The distance in pixels that the edge of the dragged item should move inside an adjacent
    ///     item to trigger a swap.
    /// </summary>
    public const double SwapThreshold = 10.0;

    /// <summary>
    ///     The minimum value, any of the drag thresholds can be set to. This guarantees a
    ///     reasonable and consistent user experience.
    /// </summary>
    public const double MinThresholdValue = 4.0;
}
