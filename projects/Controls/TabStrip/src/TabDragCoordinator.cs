// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;

namespace DroidNet.Controls;

/// <summary>
///     Application-wide drag coordinator that maintains active drag state across all <see
///     cref="TabStrip"/> instances within the process. It serializes drag lifecycle operations
///     (start/move/end) and drives the <see cref="IDragVisualService"/>.
/// </summary>
public partial class TabDragCoordinator
{
    private const double PollingIntervalMs = 1000.0 / 60.0;

    private readonly ILogger logger;
    private readonly Lock syncLock = new();
    private readonly IDragVisualService dragService;
    private readonly ReorderStrategy reorderStrategy;
    private readonly TearOutStrategy tearOutStrategy;
    private readonly List<WeakReference<TabStrip>> registeredStrips = [];

    // Drag state
    private bool isActive;
    private DragContext? dragContext;
    private IDragStrategy? currentStrategy;
    private DispatcherQueueTimer? pollingTimer;
    private Windows.Foundation.Point lastCursorPosition;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabDragCoordinator"/> class.
    /// </summary>
    /// <param name="dragService">The drag visual service.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public TabDragCoordinator(IDragVisualService dragService, ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<TabDragCoordinator>() ?? NullLoggerFactory.Instance.CreateLogger<TabDragCoordinator>();
        this.dragService = dragService;
        this.reorderStrategy = new ReorderStrategy(loggerFactory);
        this.tearOutStrategy = new TearOutStrategy(dragService, this, loggerFactory);
        this.LogCreated();
    }

    /// <summary>
    /// Raised when the coordinator receives move updates during an active drag. Subscribers (TabStrip instances)
    /// may use this to perform hit-testing and insertion logic.
    /// </summary>
    public event EventHandler<DragMovedEventArgs>? DragMoved;

    /// <summary>
    /// Raised when the coordinator ends a drag. Subscribers should finalize insertion/removal as needed.
    /// </summary>
    public event EventHandler<DragEndedEventArgs>? DragEnded;

    /// <summary>
    /// Starts a drag operation for the provided <paramref name="item"/> originating from <paramref name="source"/>.
    /// </summary>
    /// <param name="item">Logical TabItem being dragged.</param>
    /// <param name="source">Source TabStrip.</param>
    /// <param name="visualContainer">Visual TabStripItem container for the dragged item.</param>
    /// <param name="visualDescriptor">Descriptor describing the drag visual overlay.</param>
    /// <param name="initialScreenPoint">Optional initial cursor position in screen coordinates. If null, will use GetCursorPos().</param>
    public void StartDrag(
        TabItem item,
        TabStrip source,
        TabStripItem visualContainer,
        DragVisualDescriptor visualDescriptor,
        Windows.Foundation.Point? initialScreenPoint = null)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(visualContainer);
        ArgumentNullException.ThrowIfNull(visualDescriptor);

        lock (this.syncLock)
        {
            if (this.isActive)
            {
                this.LogDragAlreadyActive();
                throw new InvalidOperationException("A drag is already active in this process.");
            }

            this.isActive = true;

            // Create spatial mapper
            var window = Native.GetWindowForElement(source);
            var spatialMapper = new SpatialMapper(window, source);

            // Store hotspot in context for use by strategies (esp. TearOutStrategy)
            this.dragContext = new DragContext(source, item, spatialMapper);
            this.lastCursorPosition = initialScreenPoint ?? GetInitialCursorPosition();
            this.SwitchToReorderMode(this.lastCursorPosition);

            // Note: Polling is NOT started here - Reorder mode uses pointer events.
            // Polling only starts when switching to TearOut mode (for cross-window resilience).
            this.LogDragStarted();
        }
    }

    /// <summary>
    /// Registers a TabStrip instance for cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to register.</param>
    public void RegisterTabStrip(TabStrip strip)
    {
        ArgumentNullException.ThrowIfNull(strip);

        lock (this.syncLock)
        {
            // Clean up dead references
            this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            // Add new reference
            this.registeredStrips.Add(new WeakReference<TabStrip>(strip));
        }
    }

    /// <summary>
    /// Unregisters a TabStrip instance from cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to unregister.</param>
    public void UnregisterTabStrip(TabStrip strip)
    {
        ArgumentNullException.ThrowIfNull(strip);

        lock (this.syncLock)
        {
            this.registeredStrips.RemoveAll(wr =>
            {
                if (!wr.TryGetTarget(out var target))
                {
                    return true; // Remove dead reference
                }

                return ReferenceEquals(target, strip);
            });
        }
    }

    /// <summary>
    /// Updates the drag operation with the current cursor position. This drives the active strategy
    /// (Reorder or TearOut) and notifies registered TabStrip instances via the DragMoved event.
    /// </summary>
    /// <param name="screenPoint">Current cursor position in physical screen pixels.</param>
    public void UpdateDragPosition(Windows.Foundation.Point screenPoint)
    {
        lock (this.syncLock)
        {
            if (!this.isActive || this.currentStrategy is null || this.dragContext is null)
            {
                this.LogDragMoveIgnored();
                return;
            }

            // Check if position changed
            if (Math.Abs(screenPoint.X - this.lastCursorPosition.X) < 0.5
                && Math.Abs(screenPoint.Y - this.lastCursorPosition.Y) < 0.5)
            {
                this.LogDragMoveIgnoredNoChange();
                return;
            }

            var previousPosition = this.lastCursorPosition;
            this.lastCursorPosition = screenPoint;

            // Delegate to current strategy (pass screen coordinates, strategy will convert)
            if (this.dragContext is not null)
            {
                var spatialPoint = new SpatialPoint<ScreenSpace>(screenPoint);
                this.currentStrategy.OnDragPositionChanged(spatialPoint);

                this.LogDragMoved(screenPoint, previousPosition);

                // Perform hit-testing for cross-TabStrip coordination
                var hitStrip = this.GetHitTestTabStrip(screenPoint);
                var isInReorderMode = this.currentStrategy == this.reorderStrategy;

                // Raise DragMoved event for subscribers (TabStrip instances will subscribe in Phase 3).
                this.DragMoved?.Invoke(
                    this,
                    new DragMovedEventArgs
                    {
                        ScreenPoint = screenPoint,
                        Item = this.dragContext.DraggedItem,
                        IsInReorderMode = isInReorderMode,
                        HitStrip = hitStrip,
                        DropIndex = null, // Will be populated by strategies in later phases
                    });
            }
        }
    }

    /// <summary>
    /// Ends the current drag operation. If <paramref name="droppedOverStrip"/> is true, the coordinator
    /// will pass <paramref name="destination"/> and <paramref name="newIndex"/> to the source for finalization.
    /// </summary>
    /// <param name="screenPoint">Screen coordinate where the drop occurred.</param>
    /// <param name="droppedOverStrip">True when the drop occurred over a TabStrip instance.</param>
    /// <param name="destination">Destination TabStrip if applicable; otherwise null.</param>
    /// <param name="newIndex">Index at which the item was inserted into the destination, or null.</param>
    public void EndDrag(Windows.Foundation.Point screenPoint, bool droppedOverStrip, TabStrip? destination, int? newIndex)
    {
        lock (this.syncLock)
        {
            if (!this.isActive || this.dragContext is null)
            {
                this.LogDragEndedIgnored();
                return;
            }

            this.StopPollingTimer();
            this.LogDragEnded(screenPoint, droppedOverStrip, destination, newIndex);

            var isInReorderMode = this.currentStrategy == this.reorderStrategy;

            // Delegate drop to current strategy to complete the drag
            var dropHandled = false;
            if (this.currentStrategy is not null)
            {
                // Pass screen coordinates - strategy will convert to its preferred space
                var spatialPoint = new SpatialPoint<ScreenSpace>(screenPoint);
                dropHandled = this.currentStrategy.CompleteDrag();
            }

            // Notify subscribers that the drag ended so they can finalize insertion/removal.
            this.DragEnded?.Invoke(
                this,
                new DragEndedEventArgs
                {
                    ScreenPoint = screenPoint,
                    Item = this.dragContext.DraggedItem,
                    IsInReorderMode = isInReorderMode,
                    DroppedOverStrip = droppedOverStrip,
                    Destination = destination,
                    NewIndex = newIndex,
                });

            this.CleanupState();
        }
    }

    /// <summary>
    /// Aborts an active drag, restoring state and ending the visual session.
    /// </summary>
    public void Abort()
    {
        lock (this.syncLock)
        {
            if (!this.isActive)
            {
                this.LogDragAbortIgnored();
                return;
            }

            this.StopPollingTimer();
            this.LogDragAborted();

            // Complete the drag with no target (abort)
            this.currentStrategy?.CompleteDrag();

            this.CleanupState();
        }
    }

    /// <summary>
    /// Requests a preview image from the source TabStrip for the dragged item.
    /// This is called by TearOutStrategy when transitioning to TearOut mode.
    /// The source TabStrip will raise its TabDragImageRequest event, allowing the application to provide a preview.
    /// </summary>
    /// <param name="descriptor">The drag visual descriptor that can be updated with a preview image.</param>
    internal void RequestPreviewImage(DragVisualDescriptor descriptor)
    {
        if (this.dragContext is null)
        {
            return;
        }

        try
        {
            // Ask the source TabStrip to raise its TabDragImageRequest event
            // This allows the application to handle the event and provide a custom preview
            this.dragContext.TabStrip?.RaiseTabDragImageRequest(this.dragContext.DraggedItem, descriptor);
        }
        catch (Exception ex)
        {
            // Log the exception but don't let it crash the drag operation
            this.logger.LogError(ex, "Exception while requesting preview image from source TabStrip");
        }
    }

    /// <summary>
    /// Requests a preview image using an explicit drag context. Useful for unit tests or
    /// direct strategy usage where the coordinator has not started a drag itself.
    /// </summary>
    /// <param name="descriptor">The drag visual descriptor to update.</param>
    /// <param name="context">The drag context providing source strip and item.</param>
    internal void RequestPreviewImage(DragVisualDescriptor descriptor, DragContext context)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentNullException.ThrowIfNull(context);

        try
        {
            context.TabStrip?.RaiseTabDragImageRequest(context.DraggedItem, descriptor);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Exception while requesting preview image from source TabStrip (explicit context)");
        }
    }

    private static Windows.Foundation.Point GetInitialCursorPosition()
        => Native.GetCursorPos(out var cursorPos)
            ? new Windows.Foundation.Point(cursorPos.X, cursorPos.Y)
            : new Windows.Foundation.Point(0, 0);

    /// <summary>
    /// Determines if the cursor is within the TearOut threshold distance from the TabStrip bounds.
    /// </summary>
    /// <param name="cursor">Cursor position in **PHYSICAL SCREEN PIXELS** (from GetCursorPos).</param>
    /// <param name="strip">TabStrip to check bounds against.</param>
    /// <returns>True if cursor is within TearOut threshold; false otherwise.</returns>
    private bool IsWithinTearOutThreshold(Windows.Foundation.Point cursor, TabStrip strip)
    {
        try
        {
            // COORDINATE SYSTEM CONTRACT (Win32-only approach):
            // - INPUT: cursor is in PHYSICAL screen pixels (from Native.GetCursorPos)
            // - Get strip bounds in PHYSICAL pixels using Win32 GetWindowRect
            // - OUTPUT: All calculations use PHYSICAL pixels - no DPI conversion needed

            // Use GetWindowRect-based method for accurate bounds (works even with RouterOutlet in XamlRoot.Content)
            var physicalBounds = Native.GetPhysicalScreenBoundsUsingWindowRect(strip);
            if (physicalBounds == null)
            {
                return false; // Failed to get window bounds
            }

            var dpi = Native.GetDpiFromXamlRoot(strip.XamlRoot);

            this.logger.LogDebug(
                "Strip bounds: Physical=[({PhysLeft}, {PhysTop}), ({PhysRight}, {PhysBottom})], DPI={Dpi}, RasterizationScale={Scale}",
                physicalBounds.Value.Left,
                physicalBounds.Value.Top,
                physicalBounds.Value.Right,
                physicalBounds.Value.Bottom,
                dpi,
                strip.XamlRoot!.RasterizationScale);

            // Expand bounds by TearOut threshold (all in PHYSICAL pixels)
            var threshold = DragThresholds.TearOutThreshold;
            var expandedLeft = physicalBounds.Value.Left - threshold;
            var expandedTop = physicalBounds.Value.Top - threshold;
            var expandedRight = physicalBounds.Value.Right + threshold;
            var expandedBottom = physicalBounds.Value.Bottom + threshold;

            // Check if cursor (PHYSICAL) is within expanded bounds (PHYSICAL)
            var isWithin = cursor.X >= expandedLeft && cursor.X <= expandedRight && cursor.Y >= expandedTop && cursor.Y <= expandedBottom;

            this.LogTearOutThresholdCheck(
                cursor.X,
                cursor.Y,
                expandedLeft,
                expandedTop,
                expandedRight,
                expandedBottom,
                threshold,
                isWithin);

            return isWithin;
        }
        catch
        {
            // If transform fails (window closed, etc.), assume not within bounds
            return false;
        }
    }

    /// <summary>
    /// Performs hit-testing to find which TabStrip (if any) is under the cursor.
    /// </summary>
    /// <param name="physicalCursor">Cursor position in **PHYSICAL SCREEN PIXELS** (from GetCursorPos).</param>
    /// <returns>TabStrip under cursor, or null if none found.</returns>
    private TabStrip? GetHitTestTabStrip(Windows.Foundation.Point physicalCursor)
    {
        lock (this.syncLock)
        {
            // Clean up dead references first
            this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            foreach (var weakRef in this.registeredStrips)
            {
                if (!weakRef.TryGetTarget(out var strip))
                {
                    continue;
                }

                try
                {
                    // Get physical screen bounds using Win32 GetWindowRect (accurate in multi-monitor setups)
                    var physicalBounds = Native.GetPhysicalScreenBoundsUsingWindowRect(strip);
                    if (physicalBounds == null)
                    {
                        continue; // Failed to get bounds, skip this strip
                    }

                    // Check if cursor (PHYSICAL) is within strip bounds (PHYSICAL)
                    if (physicalBounds.Value.Contains(physicalCursor))
                    {
                        return strip;
                    }
                }
                catch
                {
                    // Skip strips that throw (window closed, etc.)
                    continue;
                }
            }

            return null;
        }
    }

    private void SwitchToReorderMode(Windows.Foundation.Point startPoint)
    {
        // Complete current strategy (abort if switching modes)
        if (this.currentStrategy is not null && this.dragContext is not null)
        {
            var spatialPoint = new SpatialPoint<ScreenSpace>(startPoint);
            this.currentStrategy.CompleteDrag();
        }

        this.currentStrategy = this.reorderStrategy;
        if (this.dragContext is not null)
        {
            // Pass screen coordinates - strategy will convert to its preferred space
            var spatialPoint = new SpatialPoint<ScreenSpace>(startPoint);
            this.currentStrategy.InitiateDrag(this.dragContext, spatialPoint);
        }

        // Stop polling when entering Reorder mode - pointer events drive updates
        this.StopPollingTimer();
    }

    private void SwitchToTearOutMode(Windows.Foundation.Point startPoint)
    {
        // Complete current strategy (abort if switching modes)
        if (this.currentStrategy is not null && this.dragContext is not null)
        {
            var spatialPoint = new SpatialPoint<ScreenSpace>(startPoint);
            this.currentStrategy.CompleteDrag();
        }

        this.currentStrategy = this.tearOutStrategy;
        if (this.dragContext is not null)
        {
            // Pass screen coordinates - strategy will convert to its preferred space
            var spatialPoint = new SpatialPoint<ScreenSpace>(startPoint);
            this.currentStrategy.InitiateDrag(this.dragContext, spatialPoint);
        }

        // Start polling when entering TearOut mode - needed for cross-window drag resilience
        // (pointer events may stop when source window loses focus or visual is between windows)
        this.StartPollingTimer();
    }

    private void StartPollingTimer()
    {
        var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        if (dispatcherQueue is null)
        {
            this.LogPollingTimerNoDispatcher();
            return;
        }

        this.pollingTimer = dispatcherQueue.CreateTimer();
        this.pollingTimer.Interval = TimeSpan.FromMilliseconds(PollingIntervalMs); // ~60Hz
        this.pollingTimer.IsRepeating = true;
        this.pollingTimer.Tick += this.OnPollingTimerTick;
        this.pollingTimer.Start();

        this.LogPollingTimerStarted();
    }

    private void StopPollingTimer()
    {
        if (this.pollingTimer is not null)
        {
            this.pollingTimer.Stop();
            this.pollingTimer.Tick -= this.OnPollingTimerTick;
            this.pollingTimer = null;

            this.LogPollingTimerStopped();
        }
    }

    private void OnPollingTimerTick(DispatcherQueueTimer sender, object args)
    {
        lock (this.syncLock)
        {
            if (!this.isActive || this.currentStrategy is null || this.dragContext is null)
            {
                this.LogPollingTimerTickIgnored();
                return;
            }

            // Poll global cursor position (returns physical pixels)
            if (!Native.GetCursorPos(out var cursorPos))
            {
                this.LogGetCursorPosFailed();
                return;
            }

            var newPosition = new Windows.Foundation.Point(cursorPos.X, cursorPos.Y);

            // Check if position changed (compare physical pixels)
            if (Math.Abs(newPosition.X - this.lastCursorPosition.X) > 0.5
                || Math.Abs(newPosition.Y - this.lastCursorPosition.Y) > 0.5)
            {
                var previousPosition = this.lastCursorPosition;
                this.lastCursorPosition = newPosition;

                // Check for mode transitions based on cursor position
                this.CheckAndHandleModeTransitions(newPosition);

                // Delegate to current strategy (pass screen coordinates, strategy will convert)
                if (this.dragContext is not null)
                {
                    var spatialPoint = new SpatialPoint<ScreenSpace>(newPosition);
                    this.currentStrategy.OnDragPositionChanged(spatialPoint);

                    this.LogDragMoved(newPosition, previousPosition);

                    // Perform hit-testing for cross-TabStrip coordination
                    var hitStrip = this.GetHitTestTabStrip(newPosition);
                    var isInReorderMode = this.currentStrategy == this.reorderStrategy;

                    // Raise DragMoved event (consumers get physical screen coordinates)
                    this.DragMoved?.Invoke(
                        this,
                        new DragMovedEventArgs
                        {
                            ScreenPoint = newPosition,
                            Item = this.dragContext.DraggedItem,
                            IsInReorderMode = isInReorderMode,
                            HitStrip = hitStrip,
                            DropIndex = null, // Will be populated by strategies in later phases
                        });
                }
            }
        }
    }

    private void CheckAndHandleModeTransitions(Windows.Foundation.Point cursor)
    {
        if (this.dragContext is not { TabStrip: { } })
        {
            return;
        }

        var isCurrentlyInReorder = this.currentStrategy == this.reorderStrategy;
        var isWithinSourceThreshold = this.IsWithinTearOutThreshold(cursor, this.dragContext.TabStrip);

        if (isCurrentlyInReorder && !isWithinSourceThreshold)
        {
            // Transition from Reorder to TearOut when cursor leaves source strip's threshold zone
            this.SwitchToTearOutMode(cursor);
        }
        else if (!isCurrentlyInReorder)
        {
            // In TearOut mode: check if cursor enters ANY TabStrip's bounds (not just threshold)
            // Phase 4 will handle the actual re-entry logic via DragMoved event subscribers
            // Here we just check if we should switch back to Reorder mode
            var hitStrip = this.GetHitTestTabStrip(cursor);
            if (hitStrip is not null)
            {
                // Cursor is directly over a TabStrip - switch to Reorder mode
                // Phase 4 will handle item insertion/removal via event handlers
                this.SwitchToReorderMode(cursor);
            }
        }
    }

    private void CleanupState()
    {
        this.isActive = false;
        this.dragContext = null;
        this.currentStrategy = null;
        this.lastCursorPosition = default;
    }
}
