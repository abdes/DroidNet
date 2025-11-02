// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///    Partial class containing drag-and-drop lifecycle implementation for TabStrip.
///    This file handles pointer tracking, threshold-based drag initiation, and coordinator
///    event subscriptions for managing the visual overlay during drag operations.
/// </summary>
public partial class TabStrip
{
    /// <summary>
    ///     Minimum pointer displacement (in logical pixels) required to initiate a drag operation.
    ///     This prevents accidental drag starts from small pointer movements.
    /// </summary>
    private const double DragThresholdPixels = 5.0;

    private TabStripItem? draggedItem;
    private Windows.Foundation.Point dragStartPoint;
    private bool isDragThresholdExceeded;

    /// <summary>
    ///     Testable implementation of pointer pressed logic. Extracts position data
    ///     and performs drag detection initialization.
    /// </summary>
    /// <param name="hitItem">The TabStripItem under the pointer, or null if none.</param>
    /// <param name="position">The pointer position relative to this control.</param>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a Pointer object.
    /// </remarks>
    protected virtual void HandlePointerPressed(TabStripItem? hitItem, Windows.Foundation.Point position)
    {
        if (hitItem != null)
        {
            this.draggedItem = hitItem;
            this.dragStartPoint = position;
            this.isDragThresholdExceeded = false;
            this.LogPointerPressed(hitItem, position);
        }
    }

    /// <summary>
    ///     Testable implementation of pointer moved logic. Tracks displacement and checks
    ///     if the drag threshold has been exceeded.
    /// </summary>
    /// <param name="currentPoint">The current pointer position relative to this control.</param>
    /// <returns>True if the event should be marked as handled, false otherwise.</returns>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a PointerRoutedEventArgs.
    /// </remarks>
    protected virtual bool HandlePointerMoved(Windows.Foundation.Point currentPoint)
    {
        if (this.draggedItem == null || this.isDragThresholdExceeded)
        {
            return false;
        }

        var deltaX = Math.Abs(currentPoint.X - this.dragStartPoint.X);
        var deltaY = Math.Abs(currentPoint.Y - this.dragStartPoint.Y);
        var delta = Math.Sqrt((deltaX * deltaX) + (deltaY * deltaY));

        this.LogPointerMoved(this.draggedItem, currentPoint, delta);

        if (delta >= DragThresholdPixels)
        {
            this.isDragThresholdExceeded = true;
            this.LogThresholdExceeded(delta, DragThresholdPixels);
            this.BeginDrag(this.draggedItem);
            return true;
        }

        return false;
    }

    /// <summary>
    ///     Testable implementation of pointer released logic. Ends the active drag session
    ///     based on the final pointer position.
    /// </summary>
    /// <param name="screenPoint">The final pointer position in screen coordinates.</param>
    /// <returns>True if the event should be marked as handled, false otherwise.</returns>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a Pointer object.
    /// </remarks>
    protected virtual bool HandlePointerReleased(Windows.Foundation.Point screenPoint)
    {
        var handled = false;

        if (this.draggedItem != null)
        {
            this.LogPointerReleased(this.draggedItem);

            // If drag was active, end it.
            if (this.isDragThresholdExceeded && this.DragCoordinator != null)
            {
                this.DragCoordinator.EndDrag(screenPoint, droppedOverStrip: false, destination: null, newIndex: null);
                handled = true;
            }

            this.draggedItem = null;
            this.isDragThresholdExceeded = false;
        }

        return handled;
    }

    /// <summary>
    ///     Finds the TabStripItem under the pointer event location using hit-testing.
    ///     Returns null if the pointer is not over any tab item.
    /// </summary>
    /// <param name="e">The pointer event arguments.</param>
    /// <returns>The TabStripItem under the pointer, or null if none found.</returns>
    /// <remarks>
    ///     This method uses FindElementsInHostCoordinates to get all elements at the pointer position
    ///     in z-order (topmost first). We then search from topmost to bottommost for the first element
    ///     that is or contains a TabStripItem.
    /// </remarks>
    protected virtual TabStripItem? FindTabStripItemAtPoint(PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);

        // Use CommunityToolkit's FindAscendant<TabStripItem> for robust lookup
        return (e.OriginalSource as DependencyObject)?.FindAscendant<TabStripItem>();
    }

    /// <summary>
    ///     Begins a drag operation for the specified tab item. Raises TabDragImageRequest to allow
    ///     the application to provide a preview image, clears multi-selection (GUD-001), marks the
    ///     item as dragging, and initiates the coordinator session.
    /// </summary>
    /// <param name="item">The TabStripItem being dragged.</param>
    /// <remarks>
    ///     This method is protected virtual to allow tests to verify drag initiation logic
    ///     independently. Tests can override this method to inject custom drag behavior or
    ///     verify hotspot calculation, selection clearing, and coordinator interaction.
    /// </remarks>
    protected virtual void BeginDrag(TabStripItem item)
    {
        ArgumentNullException.ThrowIfNull(item);
        this.AssertUIThread();

        if (this.DragCoordinator == null || item.Item == null)
        {
            this.LogBeginDragFailed("Coordinator or Item is null");
            return;
        }

        this.LogBeginDragStarted(item);

        // Create a descriptor for the drag visual
        var descriptor = new DragVisualDescriptor
        {
            RequestedSize = new Windows.Foundation.Size(300, 100), // Default size; can be customized
        };

        // Raise TabDragImageRequest to allow app to provide preview image
        this.RaiseTabDragImageRequest(item.Item, descriptor);

        // Clear multi-selection: only the dragged item remains selected (GUD-001)
        var originalSelection = this.SelectedItem;
        this.SelectedItem = item.Item;
        this.LogSelectionCleared();

        // Mark the item as dragging
        item.IsDragging = true;

        // Calculate hotspot as the offset from item's top-left to the drag start position within that item.
        // This ensures the drag image appears aligned with the cursor at the point where the user grabbed it.
        // The hotspot is in logical pixels (WinRT convention).
        var itemCoords = item.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0));
        var hotspot = new Windows.Foundation.Point(
            this.dragStartPoint.X - itemCoords.X,
            this.dragStartPoint.Y - itemCoords.Y);

        // Start the drag session via coordinator
        try
        {
            this.DragCoordinator.StartDrag(item.Item, this, descriptor, hotspot);
            this.LogDragSessionStarted();
        }
        catch (InvalidOperationException ex)
        {
            // Session already active in another part of the process; restore state and log
            item.IsDragging = false;
            this.SelectedItem = originalSelection;
            this.LogDragSessionFailure(ex);
        }
    }

    private void OnPreviewPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        var hitItem = this.FindTabStripItemAtPoint(e);
        var position = e.GetCurrentPoint(this).Position;

        this.HandlePointerPressed(hitItem, position);

        // If a drag started, capture the pointer for tracking across boundaries
        if (this.draggedItem != null)
        {
            _ = this.CapturePointer(e.Pointer);
        }
    }

    /// <summary>
    ///     Handles preview (tunneling) pointer moved events. Tracks pointer movement and initiates
    ///     drag when movement exceeds the threshold.
    /// </summary>
    /// <param name="sender">The event source.</param>
    /// <param name="e">Pointer event arguments.</param>
    /// <remarks>
    ///     This handler uses tunneling (preview) events to track pointer movement continuously,
    ///     even when TabStripItem children would normally handle the events.
    /// </remarks>
    private void OnPreviewPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        if (this.draggedItem == null || this.isDragThresholdExceeded)
        {
            return;
        }

        var currentPoint = e.GetCurrentPoint(this).Position;
        var handled = this.HandlePointerMoved(currentPoint);
        e.Handled = handled;
    }

    private void OnPreviewPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        var screenPoint = e.GetCurrentPoint(relativeTo: null).Position;

        var handled = this.HandlePointerReleased(screenPoint);
        e.Handled = handled;

        // Release pointer capture if drag was active
        if (this.draggedItem == null)
        {
            this.ReleasePointerCapture(e.Pointer);
        }
    }

    /// <summary>
    ///     Called when the coordinator property changes. Manages subscription/unsubscription from
    ///     coordinator events atomically: unsubscribes from the old coordinator and subscribes to
    ///     the new one.
    /// </summary>
    /// <param name="oldCoordinator">The previous coordinator, if any.</param>
    /// <param name="newCoordinator">The new coordinator, if any.</param>
    private void OnDragCoordinatorChanged(TabDragCoordinator? oldCoordinator, TabDragCoordinator? newCoordinator)
    {
        this.AssertUIThread();

        // Unsubscribe from old coordinator
        if (oldCoordinator != null)
        {
            oldCoordinator.DragMoved -= this.OnCoordinatorDragMoved;
            oldCoordinator.DragEnded -= this.OnCoordinatorDragEnded;
            this.LogCoordinatorUnsubscribed();
        }

        // Subscribe to new coordinator
        if (newCoordinator != null)
        {
            newCoordinator.DragMoved += this.OnCoordinatorDragMoved;
            newCoordinator.DragEnded += this.OnCoordinatorDragEnded;
            this.LogCoordinatorSubscribed();
        }
    }

    /// <summary>
    ///     Coordinator event handler for drag move. Hit-testing deferred to Phase 4.
    /// </summary>
    /// <param name="sender">The coordinator raising the event.</param>
    /// <param name="e">Event arguments containing screen position.</param>
    private void OnCoordinatorDragMoved(object? sender, DragMovedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();
        this.LogCoordinatorDragMoved(e.ScreenPoint);

        // Phase 4: Implement hit-testing and placeholder insertion logic here
    }

    /// <summary>
    ///     Coordinator event handler for drag end. Restores visual state and raises completion events.
    /// </summary>
    /// <param name="sender">The coordinator raising the event.</param>
    /// <param name="e">Event arguments containing drop location and destination info.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(category: "Design", "CA1031:Do not catch general exception types", Justification = "by design, drag/drop operation will always complete")]
    private void OnCoordinatorDragEnded(object? sender, DragEndedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();
        this.LogCoordinatorDragEnded(e.ScreenPoint, e.Destination, e.NewIndex);

        if (this.draggedItem != null)
        {
            var draggedTabItem = this.draggedItem.Item;

            try
            {
                // Restore visual state
                this.draggedItem.IsDragging = false;

                if (e.DroppedOverStrip && e.Destination != null && draggedTabItem != null)
                {
                    // Drop over a TabStrip: finalize insertion and raise completion event
                    // Phase 4: Implement insertion logic here
                    this.RaiseTabDragComplete(draggedTabItem, e.Destination, e.NewIndex);
                }
                else if (draggedTabItem != null)
                {
                    // Drop outside any strip: raise tear-out event
                    this.RaiseTabTearOutRequested(draggedTabItem, e.ScreenPoint);
                }
            }
            catch (Exception ex)
            {
                this.LogDragEndException(ex);
                if (draggedTabItem != null)
                {
                    this.RaiseTabDragComplete(draggedTabItem, destination: null, newIndex: null);
                }
            }
            finally
            {
                this.draggedItem = null;
                this.isDragThresholdExceeded = false;
            }
        }
    }

    /// <summary>
    ///     Raises the TabDragImageRequest event to allow the application to supply a preview image.
    /// </summary>
    /// <param name="item">The TabItem being dragged.</param>
    /// <param name="descriptor">The drag visual descriptor to populate.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(category: "Design", "CA1031:Do not catch general exception types", Justification = "by design, drag/drop operation will always complete")]
    private void RaiseTabDragImageRequest(TabItem item, DragVisualDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(descriptor);

        try
        {
            this.TabDragImageRequest?.Invoke(
                this,
                new TabDragImageRequestEventArgs { Item = item, RequestedSize = descriptor.RequestedSize });
        }
        catch (Exception ex)
        {
            this.LogTabDragImageRequestException(ex);
        }
    }

    /// <summary>
    ///     Raises the TabDragComplete event to notify subscribers of drag completion.
    /// </summary>
    /// <param name="item">The TabItem that was dragged.</param>
    /// <param name="destination">The destination TabStrip, or null if dropped outside.</param>
    /// <param name="newIndex">The new index in the destination collection, or null.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(category: "Design", "CA1031:Do not catch general exception types", Justification = "by design, drag/drop operation will always complete")]
    private void RaiseTabDragComplete(TabItem item, TabStrip? destination, int? newIndex)
    {
        ArgumentNullException.ThrowIfNull(item);

        try
        {
            this.TabDragComplete?.Invoke(
                this,
                new TabDragCompleteEventArgs { Item = item, DestinationStrip = destination, NewIndex = newIndex });
        }
        catch (Exception ex)
        {
            this.LogTabDragCompleteException(ex);
        }
    }

    /// <summary>
    ///     Raises the TabTearOutRequested event to notify the application that a tab was dropped
    ///     outside any TabStrip and should be torn out (typically into a new window).
    /// </summary>
    /// <param name="item">The TabItem to tear out.</param>
    /// <param name="screenDropPoint">Screen coordinates where the drop occurred.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(category: "Design", "CA1031:Do not catch general exception types", Justification = "by design, drag/drop operation will always complete")]
    private void RaiseTabTearOutRequested(TabItem item, Windows.Foundation.Point screenDropPoint)
    {
        ArgumentNullException.ThrowIfNull(item);

        try
        {
            this.TabTearOutRequested?.Invoke(
                this,
                new TabTearOutRequestedEventArgs { Item = item, ScreenDropPoint = screenDropPoint });
        }
        catch (Exception ex)
        {
            this.LogTabTearOutRequestedException(ex);

            // On exception, complete the drag with null destination to signal failure
            this.RaiseTabDragComplete(item, destination: null, newIndex: null);
        }
    }

    /// <summary>
    ///     Asserts that the current thread is the UI thread. Throws if not called on the UI thread.
    /// </summary>
    private void AssertUIThread()
    {
        if (!this.DispatcherQueue.HasThreadAccess)
        {
            throw new InvalidOperationException("Drag lifecycle operations must occur on the UI thread.");
        }
    }
}
