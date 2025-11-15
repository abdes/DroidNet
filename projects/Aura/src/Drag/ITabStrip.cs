// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Drag;

#pragma warning disable SA1402 // File may only contain a single type

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
    ///     Gets the container element for this TabStrip, used for spatial mapping and coordinate conversions.
    /// </summary>
    /// <returns>The FrameworkElement that hosts this TabStrip. Never null.</returns>
    public FrameworkElement GetContainerElement();

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
    ///     Detaches a tab from its host (start of a tear-out). This is an explicit
    ///     detach request and should not be treated as a 'close' operation; it is
    ///     triggered by the coordinator when a tab begins a tear-out, and is not
    ///     vetoable.
    /// </summary>
    /// <param name="item">The model object for the item being detached.</param>
    public void DetachTab(object item);

    /// <summary>
    ///     Tears out a tab by raising the TabTearOutRequested event to notify the application
    ///     to create a new window. Called by the coordinator when a tab is dropped outside any TabStrip.
    /// </summary>
    /// <param name="item">The model object for the item being torn out.</param>
    /// <param name="dropPoint">Screen coordinates where the tab was dropped.</param>
    /// <remarks>
    ///     The application should create a new window with a TabStrip and host the tab there.
    ///     If the handler throws an exception, TryCompleteDrag is called with null destination.
    /// </remarks>
    public void TearOutTab(object item, SpatialPoint<ScreenSpace> dropPoint);

    /// <summary>
    ///     Completes a drag operation and raises the control's <c>TabDragComplete</c> event. Called
    ///     by the coordinator after any drag operation completes (success or error). Should not
    ///     throw exceptions.
    /// </summary>
    /// <param name="item">The model object for the item that was dragged.</param>
    /// <param name="destinationStrip">
    ///     The TabStrip where the item ended up, or null if the operation failed.
    /// </param>
    /// <param name="newIndex">
    ///     The index in the destination TabStrip's Items collection, or null if the operation failed.
    /// </param>
    public void TryCompleteDrag(object item, ITabStrip? destinationStrip, int? newIndex);

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
    ///     Attempts to retrieve the realized visual container for the item at the specified index.
    /// </summary>
    /// <param name="index">The index of the item in the Items collection.</param>
    /// <returns>
    ///     The realized container element when available; otherwise, <see langword="null"/> if the
    ///     container has not been realized yet.
    /// </returns>
    public FrameworkElement? GetContainerForIndex(int index);

    /// <summary>
    ///     Applies a horizontal translation transform to the visual container of a tab item.
    /// </summary>
    /// <param name="contentId">The stable content identifier of the item to transform.</param>
    /// <param name="offsetX">The horizontal offset in logical pixels.</param>
    /// <remarks>
    ///     Called by ReorderStrategy to animate items sliding during drag operations.
    ///     The implementation should locate the visual container for the item by matching
    ///     the ContentId from the TabItem's DataContext and update its TranslateTransform.X property.
    ///     Using ContentId instead of indices prevents desynchronization when ItemsRepeater
    ///     realizes/unrealizes elements or when items move in the collection.
    /// </remarks>
    public void ApplyTransformToItem(Guid contentId, double offsetX);

    /// <summary>
    ///     Removes an item from the TabStrip's Items collection at the specified index.
    /// </summary>
    /// <param name="index">The index of the item to remove.</param>
    /// <remarks>
    ///     Used by drag strategies when a tab must leave the strip entirely (for example,
    ///     during tear-out or when moving across TabStrip instances).
    /// </remarks>
    public void RemoveItemAt(int index);

    /// <summary>
    ///     Moves an item within the TabStrip's Items collection from one index to another.
    /// </summary>
    /// <param name="fromIndex">The current index of the item.</param>
    /// <param name="toIndex">The target index for the item.</param>
    /// <remarks>
    ///     Called by ReorderStrategy when completing an in-strip reorder to avoid removing
    ///     and reinserting the same item, which would otherwise trigger collection-change
    ///     side effects intended for new tabs.
    /// </remarks>
    public void MoveItem(int fromIndex, int toIndex);

    /// <summary>
    ///     Returns the index of the specified item in the TabStrip's Items collection.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns>The zero-based index of the item if found; otherwise, -1.</returns>
    public int IndexOf(IDragPayload item);

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

    /// <summary>
    ///     Inserts an item into the TabStrip's Items collection and asynchronously waits for the
    ///     ItemsRepeater to realize the corresponding container.
    /// </summary>
    /// <param name="index">The index where the item should be inserted.</param>
    /// <param name="item">The model object for the item to insert (typically a shallow clone for drag operations).</param>
    /// <param name="cancellationToken">A token to cancel the realization handshake (for example when the pointer moves away).</param>
    /// <param name="timeoutMs">Maximum time in milliseconds to wait for realization before timing out. Default is 500 ms.</param>
    /// <returns>
    ///     A <see cref="RealizationResult"/> describing whether the container was realized, and the realized container when available.
    /// </returns>
    public Task<RealizationResult> InsertItemAsync(int index, object item, CancellationToken cancellationToken, int timeoutMs = 500);

    /// <summary>
    ///     Prepares the TabStrip to host an externally dragged tab by inserting a temporary placeholder at the
    ///     appropriate position and realizing its visual container.
    /// </summary>
    /// <param name="payload">The shallow-cloned payload instance created by the coordinator.</param>
    /// <param name="pointerPosition">Pointer position in the TabStrip element coordinate space.</param>
    /// <param name="cancellationToken">Token used to cancel the preparation when the pointer leaves the strip.</param>
    /// <param name="timeoutMs">Maximum time in milliseconds to wait for realization before timing out. Default is 500 ms.</param>
    /// <returns>
    ///     An <see cref="ExternalDropPreparationResult"/> when the placeholder is realized; otherwise <see langword="null"/>
    ///     if the preparation was cancelled or failed.
    /// </returns>
    public Task<ExternalDropPreparationResult?> PrepareExternalDropAsync(
        object payload,
        SpatialPoint<ElementSpace> pointerPosition,
        CancellationToken cancellationToken,
        int timeoutMs = 500);
}

/// <summary>
///     Represents a snapshot of a single item's visual layout at a point in time.
///     Used by drag strategies to track and animate item positions without direct visual tree access.
/// </summary>
public class TabStripItemSnapshot
{
    /// <summary>Gets or initializes the stable content identifier of the item.</summary>
    public required Guid ContentId { get; init; }

    /// <summary>Gets or initializes the index of the item in the Items collection.</summary>
    public required int ItemIndex { get; init; }

    /// <summary>Gets or initializes the original top-left corner position in element coordinates.</summary>
    public required SpatialPoint<ElementSpace> LayoutOrigin { get; init; }

    /// <summary>Gets or initializes the actual rendered width of the item.</summary>
    public required double Width { get; init; }

    /// <summary>Gets or sets the current horizontal translation offset applied to the item.</summary>
    public double Offset { get; set; }

    /// <summary>Gets or initializes the realized container backing this snapshot when available.</summary>
    public FrameworkElement? Container { get; init; }
}

/// <summary>
///     Result returned from <see cref="ITabStrip.InsertItemAsync(int, object, CancellationToken, int)"/>.
///     Declared as a nested type to keep related types grouped with the interface.
/// </summary>
public sealed class RealizationResult
{
    /// <summary>
    ///     Outcome statuses for the realization handshake.
    /// </summary>
    public enum Status
    {
        /// <summary>Container was realized and is available.</summary>
        Realized = 0,

        /// <summary>Realization did not complete within the configured timeout.</summary>
        TimedOut = 1,

        /// <summary>The operation was cancelled.</summary>
        Cancelled = 2,

        /// <summary>An error occurred while attempting to realize the container.</summary>
        Error = 3,
    }

    /// <summary>Gets the final status of the realization handshake.</summary>
    public Status CurrentStatus { get; init; }

    /// <summary>Gets the realized container element when <see cref="CurrentStatus"/> is <see cref="Status.Realized"/>; otherwise null.</summary>
    public FrameworkElement? Container { get; init; }

    /// <summary>Gets the exception that occurred during realization, if any.</summary>
    public Exception? Exception { get; init; }

    /// <summary>
    ///     Convenience factory for a successful result.
    /// </summary>
    /// <param name="container">The realized container element.</param>
    /// <returns>A <see cref="RealizationResult"/> with status <see cref="Status.Realized"/>.</returns>
    public static RealizationResult Success(FrameworkElement container) => new() { CurrentStatus = Status.Realized, Container = container };

    /// <summary>
    ///     Convenience factory for a failed result.
    /// </summary>
    /// <param name="status">The failure status (TimedOut, Cancelled, or Error).</param>
    /// <param name="ex">Optional exception associated with the failure.</param>
    /// <returns>A <see cref="RealizationResult"/> representing the failure.</returns>
    public static RealizationResult Failure(Status status, Exception? ex = null) => new() { CurrentStatus = status, Exception = ex };
}

/// <summary>
///     Result returned from <see cref="ITabStrip.PrepareExternalDropAsync(object, SpatialPoint{ElementSpace}, CancellationToken, int)"/>.
/// </summary>
public sealed record ExternalDropPreparationResult(int DropIndex, FrameworkElement RealizedContainer);
