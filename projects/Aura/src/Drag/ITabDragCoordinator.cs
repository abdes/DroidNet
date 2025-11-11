// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Defines the contract for coordinating drag-and-drop operations across multiple
///     <see cref="ITabStrip"/> instances within an application.
/// </summary>
/// <remarks>
///     <para>
///     The coordinator is responsible for managing the global drag state, tracking cursor
///     position, performing hit-testing across registered TabStrips, and deciding when to
///     transition between Reorder and TearOut drag modes.
///     </para>
///     <para>
///     Individual TabStrip instances register with the coordinator and receive imperative
///     method calls (OnDragEnter, OnDragOver, OnDragLeave, OnDrop) to update their local
///     UI state during drag operations.
///     </para>
/// </remarks>
public interface ITabDragCoordinator
{
    /// <summary>
    ///     Starts a drag operation for the provided <paramref name="item"/> originating
    ///     from <paramref name="source"/>.
    /// </summary>
    /// <param name="item">The logical TabItem being dragged.</param>
    /// <param name="itemIndex">The index of the item in the source TabStrip's Items collection.</param>
    /// <param name="source">The source TabStrip where the drag originated.</param>
    /// <param name="stripContainer">Container element hosting the source strip.</param>
    /// <param name="draggedElement">The visual element to render for drag preview (e.g., TabStripItem).</param>
    /// <param name="initialPosition">Initial cursor position relative to the source element.</param>
    /// <param name="hotspotOffsets">Offset from the drag hotspot to the dragged element's origin.</param>
    /// <exception cref="InvalidOperationException">
    ///     Thrown if a drag is already active in this process.
    /// </exception>
    public void StartDrag(
        IDragPayload item,
        int itemIndex,
        ITabStrip source,
        FrameworkElement stripContainer,
        FrameworkElement draggedElement,
        SpatialPoint<ElementSpace> initialPosition,
        Point hotspotOffsets);

    /// <summary>
    ///     Ends the current drag operation.
    /// </summary>
    /// <param name="screenPoint">Screen coordinate in DIP where the drop occurred.</param>
    public void EndDrag(SpatialPoint<ScreenSpace> screenPoint);

    /// <summary>
    ///     Aborts an active drag, restoring state and ending the visual session.
    /// </summary>
    public void Abort();

    /// <summary>
    ///     Registers a TabStrip instance for cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to register.</param>
    public void RegisterTabStrip(ITabStrip strip);

    /// <summary>
    ///     Unregisters a TabStrip instance from cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to unregister.</param>
    public void UnregisterTabStrip(ITabStrip strip);
}
