// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Defines the contract for drag strategies that manage the complete lifecycle of tab dragging
///     within or out of a <see cref="ITabStrip"/>.
/// </summary>
/// <remarks>
///     A drag operation begins when the user initiates a drag, causing <see cref="InitiateDrag"/>
///     to be called. The strategy prepares any required visual state and context for the operation.
///     As the pointer moves, <see cref="OnDragPositionChanged"/> is invoked, allowing the strategy
///     to update visual feedback and internal state in real time. The operation concludes with a
///     call to <see cref="CompleteDrag"/>, at which point the strategy finalizes changes and resets
///     its state, ready for subsequent drags. Implementations may tailor these stages to support a
///     variety of drag-and-drop behaviors.
/// </remarks>
internal interface IDragStrategy
{
    /// <summary>
    ///     Begins a new drag operation using this strategy. This method initializes any required
    ///     visual state and prepares the strategy to track pointer movement.
    /// </summary>
    /// <param name="context">
    ///     The contextual information for the drag operation. This object is valid only for the
    ///     duration of the drag, from the invocation of <see cref="InitiateDrag"/> until <see
    ///     cref="CompleteDrag"/> returns.
    /// </param>
    /// <param name="position">
    ///     The initial pointer position, represented as a <see cref="SpatialPoint{TSpace}"/> in screen space.
    /// </param>
    /// <exception cref="InvalidOperationException">
    ///     Thrown if a drag operation is already in progress and has not yet been completed.
    /// </exception>
    /// <exception cref="ArgumentNullException">
    ///     Thrown if <paramref name="context"/> is <see langword="null"/>.
    /// </exception>
    public void InitiateDrag(DragContext context, SpatialPoint<ScreenSpace> position);

    /// <summary>
    ///     Updates the drag operation in response to pointer movement. This method should be called
    ///     whenever the pointer moves during an active drag, allowing the strategy to update visual
    ///     feedback and internal state.
    /// </summary>
    /// <param name="position">
    ///     The current pointer position, represented as a <see cref="SpatialPoint{TSpace}"/> in screen space.
    /// </param>
    /// <remarks>
    ///     This method has no effect if a drag operation is not currently in progress.
    /// </remarks>
    public void OnDragPositionChanged(SpatialPoint<ScreenSpace> position);

    /// <summary>
    ///     Finalizes the drag operation and applies any resulting changes. Drag operations cannot
    ///     be cancelled and must always be completed. For every call to <see cref="InitiateDrag"/>,
    ///     there must be a corresponding call to <see cref="CompleteDrag"/>.
    /// </summary>
    /// <returns>
    ///     The final drop index in the destination TabStrip's Items collection, or <see langword="null"/>
    ///     if the strategy does not produce a final index (e.g., TearOut mode where the item is dropped
    ///     outside any TabStrip).
    /// </returns>
    /// <remarks>
    ///     The specific behavior upon completion may vary depending on the strategy and context.
    ///     After completion, further calls to <see cref="OnDragPositionChanged"/> or <see
    ///     cref="CompleteDrag"/> have no effect until a new drag is initiated.
    /// </remarks>
    public int? CompleteDrag();
}
