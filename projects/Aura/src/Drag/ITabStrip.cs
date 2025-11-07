// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Defines the contract between <see cref="TabDragCoordinator"/> and TabStrip implementations.
///     This interface exposes only the operations required for coordinating drag-and-drop
///     operations across multiple TabStrip instances.
/// </summary>
/// <remarks>
///     <para><b>Coordinator responsibilities:</b></para>
///     <list type="bullet">
///         <item>Track global cursor position via polling or pointer events</item>
///         <item>Perform hit-testing to determine which TabStrip is under the cursor</item>
///         <item>Decide when to transition between Reorder and TearOut modes</item>
///         <item>Coordinate visual feedback during drag operations</item>
///         <item>Manage active drag state across all registered TabStrip instances</item>
///         <item>Create and manage spatial mappers for registered TabStrip instances</item>
///     </list>
///     <para><b>TabStrip responsibilities:</b></para>
///     <list type="bullet">
///         <item>Initiate drag operations when user begins dragging a tab</item>
///         <item>Provide visual elements and preview images when requested</item>
///         <item>Handle coordinator events to update local UI state (placeholders, etc.)</item>
///         <item>Raise application-level events for drag completion and tear-out</item>
///         <item>Respond to hit-test queries from the coordinator</item>
///     </list>
///     <para><b>TabStrip should NOT:</b></para>
///     <list type="bullet">
///         <item>Perform hit-testing for cross-TabStrip drag coordination</item>
///         <item>Listen for global pointer/cursor position</item>
///         <item>Make decisions about mode transitions (Reorder/TearOut)</item>
///         <item>Directly coordinate with other TabStrip instances</item>
///         <item>Create or manage spatial mappers (coordinator's responsibility)</item>
///     </list>
/// </remarks>
public interface ITabStrip
{
    /// <summary>
    ///     Gets the name of the TabStrip instance for logging and diagnostics.
    /// </summary>
    public string Name { get; }

    /// <summary>
    ///     Performs hit-testing to determine if a point in element coordinates is within this TabStrip's bounds.
    /// </summary>
    /// <param name="elementPoint">Point in the tab strip element coordinates space.</param>
    /// <returns>
    ///     <see langword="true"/> if the point is within the TabStrip's bounds;
    ///     <see langword="false"/> otherwise.
    /// </returns>
    /// <remarks>
    ///     <para>
    ///     Called by the coordinator during drag operations to determine which TabStrip (if any)
    ///     is under the cursor. This enables cross-window drag scenarios.
    ///     </para>
    ///     <para>
    ///     The implementation should use the TabStrip's spatial mapper to convert the screen point
    ///     to element coordinates, then check if it falls within the element's bounds (0,0 to ActualWidth,ActualHeight).
    ///     </para>
    /// </remarks>
    //public bool HitTest(SpatialPoint<ElementSpace> elementPoint);

    /// <summary>
    ///     Performs hit-testing with an inset threshold to determine if a point is well within the TabStrip's bounds.
    /// </summary>
    /// <param name="elementPoint">Point in the tab strip element coordinates space.</param>
    /// <param name="threshold">
    ///     Minimum distance in logical pixels from any edge of the TabStrip.
    ///     For example, a threshold of 50 means the point must be at least 50 DIPs away from
    ///     all edges (top, bottom, left, right) of the TabStrip to be considered a hit.
    /// </param>
    /// <returns>
    ///     <see langword="true"/> if the point is within the TabStrip's bounds AND at least
    ///     <paramref name="threshold"/> pixels from all edges; <see langword="false"/> otherwise.
    /// </returns>
    /// <remarks>
    ///     Called by the coordinator to determine when to transition from TearOut back to Reorder mode.
    ///     The threshold creates an "inner zone" that ensures the cursor is sufficiently inside the
    ///     TabStrip before re-entering reorder mode, preventing rapid mode flickering at the edges.
    /// </remarks>
    public int HitTestWithThreshold(SpatialPoint<ElementSpace> elementPoint, double threshold);

    /// <summary>
    ///     Requests a preview image for the specified tab item during drag operations.
    ///     This method raises the TabDragImageRequest event to allow the application to
    ///     provide a custom preview image.
    /// </summary>
    /// <param name="item">The model object for the item being dragged.</param>
    /// <param name="descriptor">
    ///     The <see cref="DragVisualDescriptor"/> that should be updated with the preview image.
    /// </param>
    /// <remarks>
    ///     Called by the coordinator (typically via <see cref="TearOutStrategy"/>) when
    ///     transitioning from Reorder to TearOut mode to obtain a visual representation
    ///     of the dragged tab for the overlay window.
    /// </remarks>
    public void RequestPreviewImage(object item, DragVisualDescriptor descriptor);

    /// <summary>
    ///     Closes a tab by raising the TabCloseRequested event to notify the application.
    ///     Called by the coordinator when transitioning from Reorder to TearOut mode.
    /// </summary>
    /// <param name="item">The model object for the item being closed.</param>
    /// <remarks>
    ///     The application is expected to remove the tab from its model/collection.
    ///     The control does not remove the item automatically.
    ///     If the handler throws an exception, the drag operation is aborted.
    /// </remarks>
    public void CloseTab(object item);

    /// <summary>
    ///     Tears out a tab by raising the TabTearOutRequested event to notify the application
    ///     to create a new window. Called by the coordinator when a tab is dropped outside any TabStrip.
    /// </summary>
    /// <param name="item">The model object for the item being torn out.</param>
    /// <param name="dropPoint">Screen coordinates where the tab was dropped.</param>
    /// <remarks>
    ///     The application should create a new window with a TabStrip and host the tab there.
    ///     If the handler throws an exception, CompleteDrag is called with null destination.
    /// </remarks>
    public void TearOutTab(object item, SpatialPoint<ScreenSpace> dropPoint);

    /// <summary>
    ///     Completes a drag operation by raising the TabDragComplete event.
    ///     Called by the coordinator after any drag operation completes (success or error).
    /// </summary>
    /// <param name="item">The model object for the item that was dragged.</param>
    /// <param name="destinationStrip">
    ///     The TabStrip where the item ended up, or null if the operation failed.
    /// </param>
    /// <param name="newIndex">
    ///     The index in the destination TabStrip's Items collection, or null if the operation failed.
    /// </param>
    /// <remarks>
    ///     This event is always raised at the end of a drag operation, regardless of success or failure.
    ///     A null destinationStrip or newIndex indicates an error or abort condition.
    /// </remarks>
    public void CompleteDrag(object item, ITabStrip? destinationStrip, int? newIndex);

    /// <summary>
    ///     Takes a snapshot of the current visual layout of items in the TabStrip.
    ///     Used by ReorderStrategy to track item positions during drag operations.
    /// </summary>
    /// <returns>
    ///     A list of snapshot items containing layout information (index, position, width)
    ///     for all realized items in the TabStrip, ordered by their visual X position.
    /// </returns>
    /// <remarks>
    ///     Called by ReorderStrategy when initiating a drag to capture the initial layout
    ///     state. The strategy uses this to calculate displacement and animation offsets
    ///     without needing direct access to the visual tree.
    /// </remarks>
    public IReadOnlyList<TabStripItemSnapshot> TakeSnapshot();

    /// <summary>
    ///     Applies a horizontal translation transform to the visual container of a tab item.
    /// </summary>
    /// <param name="itemIndex">The index of the item in the Items collection.</param>
    /// <param name="offsetX">The horizontal offset in logical pixels.</param>
    /// <remarks>
    ///     Called by ReorderStrategy to animate items sliding during drag operations.
    ///     The implementation should locate the visual container for the item and update
    ///     its TranslateTransform.X property.
    /// </remarks>
    public void ApplyTransformToItem(int itemIndex, double offsetX);

    /// <summary>
    ///     Removes an item from the TabStrip's Items collection at the specified index.
    /// </summary>
    /// <param name="index">The index of the item to remove.</param>
    /// <remarks>
    ///     Called by ReorderStrategy when completing a drag operation to remove the item
    ///     from its original position before reinserting at the drop location.
    /// </remarks>
    public void RemoveItemAt(int index);

    /// <summary>
    ///     Inserts an item into the TabStrip's Items collection at the specified index.
    /// </summary>
    /// <param name="index">The index where the item should be inserted.</param>
    /// <param name="item">The model object for the item to insert.</param>
    /// <remarks>
    ///     Called by ReorderStrategy when completing a drag operation to insert the item
    ///     at its new position after removal from the original location.
    /// </remarks>
    public void InsertItemAt(int index, object item);
}

/// <summary>
///     Represents a snapshot of a single item's visual layout at a point in time.
///     Used by drag strategies to track and animate item positions without direct visual tree access.
/// </summary>
public class TabStripItemSnapshot
{
    /// <summary>Gets or initializes the index of the item in the Items collection.</summary>
    public required int ItemIndex { get; init; }

    /// <summary>Gets or initializes the original top-left corner position in element coordinates.</summary>
    public required SpatialPoint<ElementSpace> LayoutOrigin { get; init; }

    /// <summary>Gets or initializes the actual rendered width of the item.</summary>
    public required double Width { get; init; }

    /// <summary>Gets or sets the current horizontal translation offset applied to the item.</summary>
    public double Offset { get; set; }
}
