// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Drag;
using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace DroidNet.Aura.Controls;

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

    private bool isDragOngoing;
    private TabStripItem? draggedItem;
    private SpatialPoint<ScreenSpace> dragStartPoint;
    private bool isDragThresholdExceeded;
    private Point? hotspotOffsets;

    /// <summary>
    ///     Gets the currently dragged visualItem for testing purposes.
    /// </summary>
    protected TabStripItem? DraggedItem => this.draggedItem;

    /// <inheritdoc/>
    public void CloseTab(object item)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be of type TabItem.", nameof(item));
        }

        try
        {
            var eventArgs = new TabCloseRequestedEventArgs { Item = tabItem };
            this.TabCloseRequested?.Invoke(this, eventArgs);
        }
        catch (Exception ex)
        {
            this.LogTabCloseRequestedException(ex);
            throw; // Re-throw to let coordinator know the operation failed
        }
    }

    /// <inheritdoc/>
    public void CompleteDrag(object item, ITabStrip? destinationStrip, int? newIndex)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be of type TabItem.", nameof(item));
        }

        this.isDragOngoing = false;

        try
        {
            // Convert ITabStrip to TabStrip for the event args (application expects TabStrip type)
            var strip = destinationStrip as TabStrip;

            this.TabDragComplete?.Invoke(
                this,
                new TabDragCompleteEventArgs { Item = tabItem, DestinationStrip = strip, NewIndex = newIndex });
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogTabDragCompleteException(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    /// <inheritdoc/>
    public void TearOutTab(object item, SpatialPoint<ScreenSpace> dropPoint)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be of type TabItem.", nameof(item));
        }

        try
        {
            this.TabTearOutRequested?.Invoke(
                this,
                new TabTearOutRequestedEventArgs { Item = tabItem, ScreenDropPoint = dropPoint.Point });
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogTabTearOutRequestedException(ex);

            // On exception, complete the drag with null destination to signal failure
            ((ITabStrip)this).CompleteDrag(item, destinationStrip: null, newIndex: null);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    /// <summary>
    ///     Testable implementation of pointer pressed logic. Extracts position data
    ///     and performs drag detection initialization.
    /// </summary>
    /// <param name="hitItem">The TabStripItem under the pointer, or null if none.</param>
    /// <param name="position">The pointer position relative to this control.</param>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a Pointer object.
    /// </remarks>
    protected virtual void HandlePointerPressed(TabStripItem? hitItem, SpatialPoint<ScreenSpace> position)
    {
        if (hitItem != null)
        {
            // Enforce pinned tab constraint: pinned tabs cannot be dragged
            if (hitItem.Item?.IsPinned == true)
            {
                this.LogPointerPressed(hitItem, position.Point);
                return;
            }

            this.draggedItem = hitItem;
            this.dragStartPoint = position;
            this.isDragThresholdExceeded = false;
            this.LogPointerPressed(hitItem, position.Point);
        }
    }

    /// <summary>
    ///     Testable implementation of pointer moved logic. Tracks displacement, checks
    ///     if the drag threshold has been exceeded, and updates the active drag position.
    /// </summary>
    /// <param name="currentPoint">The current pointer position relative to this control.</param>
    /// <returns>True if the event should be marked as handled, false otherwise.</returns>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a PointerRoutedEventArgs.
    /// </remarks>
    protected virtual bool HandlePointerMoved(SpatialPoint<ScreenSpace> currentPoint)
    {
        if (this.draggedItem == null)
        {
            return false;
        }

        // Otherwise, check if we've exceeded the drag threshold
        var deltaX = Math.Abs(currentPoint.Point.X - this.dragStartPoint.Point.X);
        var deltaY = Math.Abs(currentPoint.Point.Y - this.dragStartPoint.Point.Y);
        var delta = Math.Sqrt((deltaX * deltaX) + (deltaY * deltaY));

        this.LogPointerMoved(this.draggedItem, currentPoint.Point, delta);

        if (delta >= DragThresholdPixels)
        {
            this.isDragThresholdExceeded = true;
            this.LogThresholdExceeded(delta, DragThresholdPixels);

            // Convert to screen coordinates at the point of threshold crossing
            this.InitiateDrag(this.draggedItem, currentPoint);
            this.draggedItem = null; // Clear to avoid re-triggering
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
    protected virtual bool HandlePointerReleased(SpatialPoint<ScreenSpace> screenPoint)
    {
        var handled = false;

        if (this.isDragOngoing)
        {
            this.LogPointerReleasedWhileDragging();

            // If drag was active, end it.
            if (this.isDragThresholdExceeded && this.DragCoordinator != null)
            {
                // The coordinator will determine the drop target via hit-testing
                // and call the appropriate ITabStrip.OnDrop method
                this.DragCoordinator.EndDrag(screenPoint);
                handled = true;
            }

        }

        this.isDragThresholdExceeded = false;
        this.draggedItem = null;

        return handled;
    }

    /// <summary>
    ///     Finds the TabStripItem under the pointer event location using hit-testing.
    ///     Returns null if the pointer is not over any tab visualItem.
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
    ///     Begins a drag operation for the specified tab visualItem. Raises TabDragImageRequest to allow
    ///     the application to provide a preview image, clears multi-selection (GUD-001), marks the
    ///     visualItem as dragging, and initiates the coordinator session.
    /// </summary>
    /// <param name="visualItem">The TabStripItem being dragged.</param>
    /// <param name="initialScreenPoint">Initial screen position where drag threshold was exceeded. If not provided, will use GetCursorPos.</param>
    /// <remarks>
    ///     This method is protected virtual to allow tests to verify drag initiation logic
    ///     independently. Tests can override this method to inject custom drag behavior or
    ///     verify windowPositionOffsets calculation, selection clearing, and coordinator interaction.
    /// </remarks>
    protected virtual void InitiateDrag(TabStripItem visualItem, SpatialPoint<ScreenSpace> initialScreenPoint)
    {
        if (this.DragCoordinator is null)
        {
            return; // No coordinator, no drag
        }

        Debug.Assert(this.hotspotOffsets is not null, "expecting hotspot offsets to be initialized before drag is initiated");
        this.AssertUIThread();

        if (visualItem is not { Item: { } tabItem })
        {
            throw new ArgumentException("TabStripItem must not be null, and have a valid Item to initiate drag.", nameof(visualItem));
        }

        this.LogInitiateDrag(visualItem.Item, initialScreenPoint);

        // Clear multi-selection: only the dragged visualItem remains selected (GUD-001)
        var originalSelection = this.SelectedItem;
        this.SelectedItem = tabItem;
        this.LogSelectionCleared();

        // Mark the visualItem as dragging
        visualItem.IsDragging = true;

        // Start the drag session via coordinator
        try
        {
            var itemIndex = this.Items.IndexOf(tabItem);
            this.DragCoordinator.StartDrag(tabItem, itemIndex, this, this, visualItem, initialScreenPoint, this.hotspotOffsets ?? new(0, 0));
            this.dragStartPoint = default; // Clear stored start point
        }
        catch (InvalidOperationException ex)
        {
            // Session already active in another part of the process; restore state and log
            visualItem.IsDragging = false;
            this.SelectedItem = originalSelection;
            this.LogInitiateDragFailed(ex);
        }

        this.isDragOngoing = true;
    }

    private void OnPreviewPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        var hitItem = this.FindTabStripItemAtPoint(e);
        var point = e.GetCurrentPoint(relativeTo: null).Position.AsScreen();

        this.hotspotOffsets = e.GetCurrentPoint(hitItem).Position;
        this.HandlePointerPressed(hitItem, point);

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

        var point = e.GetCurrentPoint(relativeTo: null).Position.AsScreen();
        e.Handled = this.HandlePointerMoved(point);
    }

    private void OnPreviewPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        // Get pointer position in LOGICAL screen coordinates (desktop-relative DIPs)
        var point = e.GetCurrentPoint(relativeTo: null).Position.AsScreen();
        e.Handled = this.HandlePointerReleased(point);

        this.ReleasePointerCapture(e.Pointer);
    }

    /// <summary>
    ///     Called when the coordinator property changes. Manages registration/unregistration
    ///     with the coordinator.
    /// </summary>
    /// <param name="oldCoordinator">The previous coordinator, if any.</param>
    /// <param name="newCoordinator">The new coordinator, if any.</param>
    private void OnDragCoordinatorChanged(ITabDragCoordinator? oldCoordinator, ITabDragCoordinator? newCoordinator)
    {
        this.AssertUIThread();

        // Unregister from old coordinator
        if (oldCoordinator != null)
        {
            oldCoordinator.UnregisterTabStrip(this);
            this.LogCoordinatorUnsubscribed();
        }

        // Register with new coordinator
        if (newCoordinator != null)
        {
            newCoordinator.RegisterTabStrip(this);
            this.LogCoordinatorSubscribed();
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
