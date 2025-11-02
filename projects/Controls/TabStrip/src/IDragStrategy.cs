// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Defines the contract for drag strategies that handle different modes of tab dragging.
///     Strategies implement the Strategy pattern to encapsulate mode-specific drag logic.
/// </summary>
internal interface IDragStrategy
{
    /// <summary>
    ///     Gets a value indicating whether this strategy is currently active and managing a drag operation.
    /// </summary>
    public bool IsActive { get; }

    /// <summary>
    ///     Initiates a drag operation with this strategy.
    ///     The strategy should initialize any necessary visual state and prepare for drag position updates.
    /// </summary>
    /// <param name="context">The context information for the drag operation.</param>
    /// <param name="initialPoint">The initial pointer position as a SpatialPoint with explicit coordinate space.</param>
    public void InitiateDrag(DragContext context, SpatialPoint initialPoint);

    /// <summary>
    ///     Reacts to pointer movement during an active drag operation.
    ///     The strategy should update visual feedback based on the new pointer position.
    /// </summary>
    /// <param name="currentPoint">The current pointer position as a SpatialPoint with explicit coordinate space.</param>
    public void OnDragPositionChanged(SpatialPoint currentPoint);

    /// <summary>
    ///     Completes the drag operation and finalizes any changes.
    ///     This is the only way to properly finish a drag - there is no separate "drop" event.
    /// </summary>
    /// <param name="finalPoint">The final pointer position as a SpatialPoint with explicit coordinate space.</param>
    /// <param name="targetStrip">The target TabStrip if dropping over one; otherwise null.</param>
    /// <param name="targetIndex">The target insertion index if applicable; otherwise null.</param>
    /// <returns>True if the drag was completed successfully; false if it was cancelled or failed.</returns>
    public bool CompleteDrag(SpatialPoint finalPoint, TabStrip? targetStrip, int? targetIndex);
}
