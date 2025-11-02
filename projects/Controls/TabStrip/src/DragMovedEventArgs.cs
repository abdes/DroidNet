// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Controls;

/// <summary>
/// Event args for drag move notifications.
/// </summary>
public sealed class DragMovedEventArgs : EventArgs
{
    /// <summary>
    /// Gets the screen point reported by the coordinator.
    /// </summary>
    public Windows.Foundation.Point ScreenPoint { get; init; }

    /// <summary>
    /// Gets the TabItem being dragged.
    /// </summary>
    public required TabItem Item { get; init; }

    /// <summary>
    /// Gets a value indicating whether the drag is currently in Reorder mode.
    /// </summary>
    public bool IsInReorderMode { get; init; }

    /// <summary>
    /// Gets the TabStrip currently under the cursor during drag, if any.
    /// </summary>
    public TabStrip? HitStrip { get; init; }

    /// <summary>
    /// Gets the potential drop index within the HitStrip, if applicable.
    /// </summary>
    public int? DropIndex { get; init; }
}
