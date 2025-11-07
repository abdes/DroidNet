// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DryIoc.ImTools;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Application-wide drag coordinator that maintains active drag state across all <see
///     cref="ITabStrip"/> instances within the process. It serializes drag lifecycle operations
///     (start/move/end) and drives the <see cref="IDragVisualService"/>.
/// </summary>
public partial class TabDragCoordinator : ITabDragCoordinator
{
    private const double PollingIntervalMs = 1000.0 / 60.0;

    private readonly ILogger logger;
    private readonly SpatialMapperFactory spatialMapperFactory;
    private readonly Lock syncLock = new();
    private readonly IDragVisualService dragService;
    private readonly ReorderStrategy reorderStrategy;
    private readonly TearOutStrategy tearOutStrategy;
    private readonly IWindowManagerService windowManager;
    private readonly List<WeakReference<ITabStrip>> registeredStrips = [];

    // Drag state
    private bool isActive;
    private DragContext? dragContext;
    private IDragStrategy? currentStrategy;
    private DispatcherQueueTimer? pollingTimer;
    private SpatialPoint<PhysicalScreenSpace> lastCursorPosition;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabDragCoordinator"/> class.
    /// </summary>
    /// <param name="windowManager">The window manager service used to manage and query open windows.</param>
    /// <param name="spatialMapperFactory">The factory used to create spatial mappers for coordinate conversions.</param>
    /// <param name="dragService">The drag visual service.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public TabDragCoordinator(
        IWindowManagerService windowManager,
        SpatialMapperFactory spatialMapperFactory,
        IDragVisualService dragService,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<TabDragCoordinator>() ?? NullLoggerFactory.Instance.CreateLogger<TabDragCoordinator>();
        this.spatialMapperFactory = spatialMapperFactory;
        this.dragService = dragService;
        this.reorderStrategy = new ReorderStrategy(loggerFactory);
        this.tearOutStrategy = new TearOutStrategy(dragService, this, loggerFactory);

        this.windowManager = windowManager;

        this.LogCreated();
    }

    /// <summary>
    /// Starts a drag operation for the provided <paramref name="item"/> originating from <paramref name="source"/>.
    /// </summary>
    /// <param name="item">Logical TabItem being dragged.</param>
    /// <param name="itemIndex">The index of the item being dragged within the source TabStrip.</param>
    /// <param name="source">Source TabStrip.</param>
    /// <param name="visualElement">Visual element for drag preview rendering.</param>
    /// <param name="hotspotOffsets">Optional initial cursor position in screen coordinates. If null, will use GetCursorPos().</param>
    public void StartDrag(
        object item,
        int itemIndex,
        ITabStrip source,
        FrameworkElement stripContainer,
        FrameworkElement draggedElement,
        SpatialPoint<ScreenSpace> initialPosition,
        Point hotspotOffsets)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(draggedElement);

        lock (this.syncLock)
        {
            if (this.isActive)
            {
                this.LogDragAlreadyActive();
                throw new InvalidOperationException("A drag is already active in this process.");
            }

            this.isActive = true;

            var mapper = this.CreateSpatialMapper(stripContainer);
            this.dragContext = new DragContext(source, item, itemIndex, hotspotOffsets, stripContainer, draggedElement, mapper);
            this.SwitchToReorderMode(mapper.ToPhysicalScreen(initialPosition));

            this.StartPollingTimer();
            this.LogDragStarted();
        }
    }

    /// <summary>
    /// Ends the current drag operation.
    /// </summary>
    /// <param name="screenPoint">Screen coordinate where the drop occurred.</param>
    public void EndDrag(SpatialPoint<ScreenSpace> screenPoint)
    {
        lock (this.syncLock)
        {
            if (!this.isActive || this.dragContext is null)
            {
                this.LogDragEndedIgnored();
                return;
            }

            this.StopPollingTimer();

            var hitStrip = this.GetHitTestTabStrip(screenPoint);
            var isTearOutMode = this.currentStrategy == this.tearOutStrategy;

            // Complete the current strategy (strategy will handle the drop)
            int? finalDropIndex = null;
            if (this.currentStrategy is not null)
            {
                finalDropIndex = this.currentStrategy.CompleteDrag(drop: true);
            }

            // Determine final outcome and raise appropriate events
            if (isTearOutMode && hitStrip is null && this.dragContext.TabStrip is not null)
            {
                // TearOut drop outside any TabStrip - raise TabTearOutRequested
                this.dragContext.TabStrip.TearOutTab(this.dragContext.DraggedItem, screenPoint);

                // TabDragComplete is raised immediately with null to signal TearOut in progress
                // Application handles window creation asynchronously
                this.dragContext.TabStrip.CompleteDrag(this.dragContext.DraggedItem, destinationStrip: null, newIndex: null);
            }
            else if (hitStrip is not null && finalDropIndex.HasValue)
            {
                // Drop into a TabStrip - raise TabDragComplete with actual final index from strategy
                hitStrip.CompleteDrag(this.dragContext.DraggedItem, hitStrip, finalDropIndex.Value);
            }
            else
            {
                // Error case - raise TabDragComplete with null destination
                this.dragContext.TabStrip?.CompleteDrag(this.dragContext.DraggedItem, destinationStrip: null, newIndex: null);
            }

            this.LogDragEnded(screenPoint.Point, hitStrip is not null, hitStrip, finalDropIndex);

            this.CleanupState();
        }
    }

    /// <summary>
    /// Registers a TabStrip instance for cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to register.</param>
    public void RegisterTabStrip(ITabStrip strip)
    {
        ArgumentNullException.ThrowIfNull(strip);

        lock (this.syncLock)
        {
            // Clean up dead references
            _ = this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            // Add new reference
            this.registeredStrips.Add(new WeakReference<ITabStrip>(strip));
        }
    }

    /// <summary>
    /// Unregisters a TabStrip instance from cross-window drag coordination.
    /// </summary>
    /// <param name="strip">The TabStrip to unregister.</param>
    public void UnregisterTabStrip(ITabStrip strip)
    {
        ArgumentNullException.ThrowIfNull(strip);

        lock (this.syncLock)
        {
            _ = this.registeredStrips.RemoveAll(wr =>
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
            _ = this.currentStrategy?.CompleteDrag(drop: false);

            this.CleanupState();
        }
    }

    private ISpatialMapper CreateSpatialMapper(FrameworkElement element)
    {
        Window? window = null;

        if (element.XamlRoot is not null)
        {
            window = this.windowManager.OpenWindows.FindFirst(context => context.Window.Content?.XamlRoot == element.XamlRoot)?.Window;

            if (window is null && element.XamlRoot.ContentIslandEnvironment is not null)
            {
                var windowIdFromIsland = element.XamlRoot.ContentIslandEnvironment.AppWindowId;
                window = this.windowManager.OpenWindows.FindFirst(context => context.Id == windowIdFromIsland)?.Window;
            }
        }

        return window is null
            ? throw new InvalidOperationException("Unable to locate the hosting window for the supplied TabStrip element. Ensure the element is loaded and registered with the WindowManager before initiating a drag.")
            : this.spatialMapperFactory(window, element);
    }

    private SpatialPoint<ScreenSpace> GetInitialCursorPosition()
    {
        if (this.dragContext is null)
        {
            throw new InvalidOperationException("DragContext must be set to get initial cursor position.");
        }

        if (!GetCursorPos(out var cursorPos))
        {
            throw new InvalidOperationException("Failed to get initial cursor position.");
        }

        // Convert to logical screen space
        var point = new Windows.Foundation.Point(cursorPos.X, cursorPos.Y).AsPhysicalScreen();
        return this.dragContext.SpatialMapper.Convert<PhysicalScreenSpace, ScreenSpace>(point);
    }

    /// <summary>
    /// Determines if the cursor is within the TearOut threshold distance from the TabStrip bounds.
    /// </summary>
    /// <param name="cursor">Cursor position in screen coordinates.</param>
    /// <param name="strip">TabStrip to check bounds against.</param>
    /// <returns>True if cursor is within TearOut threshold; false otherwise.</returns>
    private bool ExceedsTearOutThreshold(SpatialPoint<ScreenSpace> cursor, ITabStrip? strip)
    {
        if (this.dragContext is null || strip is null)
        {
            return false;
        }

        var stripPoint = this.dragContext.SpatialMapper.Convert<ScreenSpace, ElementSpace>(cursor);
        return strip.HitTestWithThreshold(stripPoint, DragThresholds.TearOutThreshold) > 0;
    }

    /// <summary>
    /// Performs hit-testing to find which TabStrip (if any) is under the cursor.
    /// </summary>
    /// <param name="screenCursor">Cursor position in screen coordinates.</param>
    /// <returns>TabStrip under cursor, or null if none found.</returns>
    private ITabStrip? GetHitTestTabStrip(SpatialPoint<ScreenSpace> screenCursor)
    {
        if (this.dragContext is null)
        {
            return null;
        }

        lock (this.syncLock)
        {
            // Clean up dead references first
            _ = this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            foreach (var weakRef in this.registeredStrips)
            {
                if (!weakRef.TryGetTarget(out var strip))
                {
                    continue;
                }

                // Convert to the tab strip space for hit-testing
                var elementPoint = this.dragContext.SpatialMapper.Convert<ScreenSpace, ElementSpace>(screenCursor);
                if (strip.HitTestWithThreshold(elementPoint, DragThresholds.TearOutThreshold) >= 0)
                {
                    return strip;
                }
            }

            return null;
        }
    }

    private void SwitchToReorderMode(SpatialPoint<PhysicalScreenSpace> position)
    {
        // Complete current strategy (abort if switching modes)
        if (this.currentStrategy is not null && this.dragContext is not null)
        {
            _ = this.currentStrategy.CompleteDrag(drop: false);
        }

        this.currentStrategy = this.reorderStrategy;
        if (this.dragContext is not null)
        {
            // Pass screen coordinates - strategy will convert to its preferred space
            this.currentStrategy.InitiateDrag(this.dragContext, position);
        }
    }

    private void SwitchToTearOutMode(SpatialPoint<PhysicalScreenSpace> position)
    {
        // Complete current strategy (abort if switching modes)
        if (this.currentStrategy is not null && this.dragContext is not null)
        {
            _ = this.currentStrategy.CompleteDrag(drop: false);
        }

        // Close the tab before transitioning to TearOut mode
        if (this.dragContext is { TabStrip: { }, DraggedItem: { } })
        {
            try
            {
                this.dragContext.TabStrip.CloseTab(this.dragContext.DraggedItem);
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                // If CloseTab handler throws, abort the drag
                this.LogTabCloseRequestedFailed(ex);
                this.dragContext.TabStrip.CompleteDrag(
                    this.dragContext.DraggedItem,
                    this.dragContext.TabStrip,
                    this.dragContext.DraggedItemIndex);
                this.CleanupState();
                return;
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        this.currentStrategy = this.tearOutStrategy;
        if (this.dragContext is not null)
        {
            // Pass screen coordinates - strategy will convert to its preferred space
            this.currentStrategy.InitiateDrag(this.dragContext, position);
        }
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
            if (!GetCursorPos(out var cursorPos))
            {
                this.LogGetCursorPosFailed();
                return;
            }

            var newPosition = new Windows.Foundation.Point(cursorPos.X, cursorPos.Y);

            // Check if position changed (compare physical pixels)
            if (Math.Abs(newPosition.X - this.lastCursorPosition.Point.X) > 0.5
                || Math.Abs(newPosition.Y - this.lastCursorPosition.Point.Y) > 0.5)
            {
                var previousPosition = this.lastCursorPosition;
                this.lastCursorPosition = newPosition.AsPhysicalScreen();

                // Check for mode transitions based on cursor position
                this.CheckAndHandleModeTransitions(newPosition.AsPhysicalScreen());

                // Delegate to current strategy (pass screen coordinates, strategy will convert)
                if (this.dragContext is not null)
                {
                    this.currentStrategy.OnDragPositionChanged(newPosition.AsPhysicalScreen());

                    this.LogDragMoved(newPosition, previousPosition.Point);
                }
            }
        }
    }

    private void CheckAndHandleModeTransitions(SpatialPoint<PhysicalScreenSpace> cursor)
    {
        if (this.dragContext is not { TabStrip: { } })
        {
            return;
        }

        var isCurrentlyInReorder = this.currentStrategy == this.reorderStrategy;
        var stripPoint = this.dragContext.SpatialMapper.Convert<PhysicalScreenSpace, ElementSpace>(cursor);
        var hitTest = this.dragContext.TabStrip.HitTestWithThreshold(stripPoint, DragThresholds.TearOutThreshold);

        if (isCurrentlyInReorder && hitTest < 0)
        {
            // Transition from Reorder to TearOut when cursor leaves source strip's threshold zone
            this.SwitchToTearOutMode(cursor);
        }
        else if (!isCurrentlyInReorder && hitTest > 0)
        {
            // Cursor is directly over a TabStrip - switch to Reorder mode
            // Phase 4 will handle item insertion/removal via event handlers
            this.SwitchToReorderMode(cursor);
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
