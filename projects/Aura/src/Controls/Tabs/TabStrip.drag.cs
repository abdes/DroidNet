// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Aura.Drag;
using DroidNet.Coordinates;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Aura.Controls;

/// <summary>
///    Partial class containing drag-and-drop lifecycle implementation for TabStrip.
///    This file handles pointer tracking, threshold-based drag initiation, and coordinator
///    event subscriptions for managing the visual overlay during drag operations.
/// </summary>
public partial class TabStrip
{
    private SpatialPoint<ElementSpace> dragStartPoint; // Pointer position at initial press
    private TabStripItem? draggedItem; // Item being considered for drag
    private Point? hotspotOffsets; // Offset from pointer to top-left of dragged item at press time

    /// <summary>
    ///     Gets a value indicating whether a drag operation is engaged for an item and awaiting
    ///     initiation.
    /// </summary>
    /// <seealso cref="DragThreshold"/>
    /// <seealso cref="InitiateDrag"/>
    protected bool IsDragEngaged => this.DragCoordinator is not null && this.draggedItem is not null;

    /// <summary>
    ///     Gets a value indicating whether a drag operation has been initiated, and is currently
    ///     ongoing.
    /// </summary>
    /// <seealso cref="InitiateDrag"/>
    /// <seealso cref="TryCompleteDrag"/>
    protected bool IsDragOngoing { get; private set; }

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
        catch (Exception ex)
        {
            this.LogTabTearOutRequestedException(ex);
            throw; // Re-throw to let coordinator know the operation failed
        }
    }

    /// <inheritdoc/>
    public void DetachTab(object item)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be of type TabItem.", nameof(item));
        }

        try
        {
            this.TabDetachRequested?.Invoke(this, new TabDetachRequestedEventArgs { Item = tabItem });
        }
        catch (Exception ex)
        {
            this.LogTabDetachRequestedException(ex);
            throw;
        }
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "method not expected to throw")]
    public void TryCompleteDrag(object item, ITabStrip? destinationStrip, int? newIndex)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be of type TabItem.", nameof(item));
        }

        this.IsDragOngoing = false;

        this.ClearDraggingVisualState(tabItem);

        try
        {
            // Convert ITabStrip to TabStrip for the event args (application expects TabStrip type)
            var strip = destinationStrip as TabStrip;

            this.TabDragComplete?.Invoke(
                this,
                new TabDragCompleteEventArgs { Item = tabItem, DestinationStrip = strip, NewIndex = newIndex });
        }
        catch (Exception ex)
        {
            // By design, a drag operation must always complete, with or without errors. Simply log
            // and swallow the exceptions.
            this.LogTabDragCompleteException(ex);
        }
    }

    /// <summary>
    ///     Testable implementation of pointer pressed logic. Extracts position data
    ///     and performs drag detection initialization.
    /// </summary>
    /// <param name="hitItem">The TabStripItem under the pointer.</param>
    /// <param name="position">The pointer position relative to this control.</param>
    /// <param name="hotspotOffsets">The offset from the pointer to the top-left of the dragged item at press time.</param>
    /// <remarks>
    ///     This method can be called directly in tests without requiring a Pointer object.
    /// </remarks>
    protected virtual void HandlePointerPressed(TabStripItem hitItem, SpatialPoint<ElementSpace> position, Point hotspotOffsets)
    {
        Debug.Assert(hitItem is not null, "hitItem must not be null in HandlePointerPressed");

        this.LogPointerPressed(hitItem, position, hotspotOffsets);

        // Enforce pinned tab constraint: pinned tabs cannot be dragged
        if (hitItem.Item?.IsPinned == true)
        {
            return;
        }

        this.draggedItem = hitItem;

        // Remember the drag start point and the hotspot offsets for use during drag initiation
        this.hotspotOffsets = hotspotOffsets;
        this.dragStartPoint = position;
    }

    /// <summary>
    ///     Tracks displacement relative to the initial pointer-pressed point, checks if the drag
    ///     threshold has been exceeded, and updates the active drag position.
    /// </summary>
    /// <param name="currentPoint">The current pointer position relative to this control.</param>
    /// <returns>True if the event should be marked as handled, false otherwise.</returns>
    /// <remarks>
    ///     Testable implementation of pointer moved logic; can be called directly in tests without
    ///     requiring a PointerRoutedEventArgs.
    /// </remarks>
    protected virtual bool HandlePointerMoved(SpatialPoint<ElementSpace> currentPoint)
    {
        if (this.DragCoordinator is null || this.draggedItem == null)
        {
            return false;
        }

        // Otherwise, check if we've exceeded the drag threshold
        var deltaX = Math.Abs(currentPoint.Point.X - this.dragStartPoint.Point.X);
        var deltaY = Math.Abs(currentPoint.Point.Y - this.dragStartPoint.Point.Y);
        var delta = Math.Sqrt((deltaX * deltaX) + (deltaY * deltaY));

        this.LogPointerMoved(this.draggedItem, currentPoint.Point, delta);

        if (delta >= this.DragThreshold)
        {
            this.LogThresholdExceeded(delta, this.DragThreshold);

            this.draggedItem.IsDragging = true;
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

        if (this.IsDragOngoing)
        {
            Debug.Assert(this.DragCoordinator is not null, "expecting drag coordinator to be non-null during active drag");
            this.LogPointerReleasedWhileDragging();

            // The coordinator will determine the drop target via hit-testing
            // and call the appropriate ITabStrip.OnDrop method
            this.DragCoordinator.EndDrag(screenPoint);
            handled = true;
        }

        this.draggedItem = null;

        return handled;
    }

    /// <summary>
    ///     Begins a drag operation for the specified tab visualItem. Raises TabDragImageRequest to allow
    ///     the application to provide a preview image, clears multi-selection (GUD-001), marks the
    ///     visualItem as dragging, and initiates the coordinator session.
    /// </summary>
    /// <param name="visualItem">The TabStripItem being dragged.</param>
    /// <param name="initialHitPoint">Initial screen position where drag threshold was exceeded. If not provided, will use GetCursorPos.</param>
    /// <remarks>
    ///     This method is protected virtual to allow tests to verify drag initiation logic
    ///     independently. Tests can override this method to inject custom drag behavior or
    ///     verify windowPositionOffsets calculation, selection clearing, and coordinator interaction.
    /// </remarks>
    protected virtual void InitiateDrag(TabStripItem visualItem, SpatialPoint<ElementSpace> initialHitPoint)
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

        this.LogInitiateDrag(visualItem.Item, initialHitPoint);

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
            this.DragCoordinator.StartDrag(tabItem, itemIndex, this, this, visualItem, initialHitPoint, this.hotspotOffsets ?? new(0, 0));
            this.dragStartPoint = default; // Clear stored start point
        }
        catch (InvalidOperationException ex)
        {
            // Session already active in another part of the process; restore state and log
            visualItem.IsDragging = false;
            this.SelectedItem = originalSelection;
            this.LogTryCompleteDragFailed(tabItem.ContentId, ex);
        }

        this.IsDragOngoing = true;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Cleanup should never surface to callers")]
    private void ClearDraggingVisualState(TabItem tabItem)
    {
        // Clear IsDragging on any prepared containers that correspond to the supplied
        // dropped item. We must not keep historical drag state on the TabStrip; only
        // use the item supplied to this method to locate matching visuals.
        try
        {
            var targetContentId = tabItem.ContentId;
            var clearedCount = 0;

            for (var i = 0; i < this.realizedItems.Count; i++)
            {
                var info = this.realizedItems[i];
                if (info.Element is not Grid grid)
                {
                    continue;
                }

                var tsi = grid.Children.OfType<TabStripItem>().FirstOrDefault();
                if (tsi is not { Item: { } tsiItem } || tsiItem.ContentId != targetContentId)
                {
                    continue;
                }

                if (tsi.IsDragging)
                {
                    tsi.IsDragging = false;
                    clearedCount++;
                }
            }

            this.LogDragStateCleanup(tabItem, clearedCount);
        }
        catch (Exception ex)
        {
            // Swallow non-fatal exceptions during cleanup but log for diagnostics.
            Debug.WriteLine($"[TabStrip] TryCompleteDrag: failed clearing IsDragging for item (ContentId={tabItem.ContentId}): {ex}");
        }
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
