// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
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
    private const double PollingIntervalMs = 1000.0 / 120.0;

    private readonly ILogger logger;
    private readonly SpatialMapperFactory spatialMapperFactory;
    private readonly Lock syncLock = new();
    private readonly IDragVisualService dragService;
    private readonly ReorderStrategy reorderStrategy;
    private readonly TearOutStrategy tearOutStrategy;
    private readonly IWindowManagerService windowManager;
    private readonly List<WeakReference<ITabStrip>> registeredStrips = [];
    private readonly DispatcherQueue dispatcherQueue;

    // Pending insert CancellationTokenSources keyed by payload ContentId. Used to signal
    // cancellation to a control-owned InsertItemAsync operation when the coordinator
    // detaches or the pointer moves away.
    private readonly Dictionary<Guid, CancellationTokenSource> pendingInsertCts = [];

    // Drag state
    private bool isActive;
    private DragContext? dragContext;
    private IDragStrategy? currentStrategy;
    private DispatcherQueueTimer? pollingTimer;
    private SpatialPoint<PhysicalScreenSpace> lastCursorPosition;
    private bool isAttaching; // Prevents concurrent AttachToStrip calls

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabDragCoordinator"/> class.
    ///     Coordinates drag operations across all <see cref="ITabStrip"/> instances in the application.
    /// </summary>
    /// <param name="hosting">The hosting context providing the dispatcher queue.</param>
    /// <param name="windowManager">Service for managing and querying open windows.</param>
    /// <param name="spatialMapperFactory">Factory for creating spatial mappers for coordinate conversions.</param>
    /// <param name="dragService">Service for managing drag visuals.</param>
    /// <param name="loggerFactory">Optional logger factory for diagnostics.</param>
    public TabDragCoordinator(
        HostingContext hosting,
        IWindowManagerService windowManager,
        SpatialMapperFactory spatialMapperFactory,
        IDragVisualService dragService,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<TabDragCoordinator>() ?? NullLoggerFactory.Instance.CreateLogger<TabDragCoordinator>();
        this.dispatcherQueue = hosting.Dispatcher;
        this.spatialMapperFactory = spatialMapperFactory;
        this.dragService = dragService;
        this.reorderStrategy = new ReorderStrategy(loggerFactory);
        this.tearOutStrategy = new TearOutStrategy(dragService, this, loggerFactory);

        this.windowManager = windowManager;

        this.LogCreated();
    }

    /// <summary>
    ///     Starts a drag operation for the specified tab item from the given source strip.
    /// </summary>
    /// <param name="item">The logical tab item being dragged.</param>
    /// <param name="itemIndex">Index of the item in the source strip.</param>
    /// <param name="source">Source <see cref="ITabStrip"/> instance.</param>
    /// <param name="stripContainer">Container element hosting the source strip.</param>
    /// <param name="draggedElement">Visual element for drag preview.</param>
    /// <param name="initialPosition">Initial pointer position relative to the source element.</param>
    /// <param name="hotspotOffsets">Offset from the drag hotspot to the dragged element's origin.</param>
    public void StartDrag(
        IDragPayload item,
        int itemIndex,
        ITabStrip source,
        FrameworkElement stripContainer,
        FrameworkElement draggedElement,
        SpatialPoint<ElementSpace> initialPosition,
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

            var mapper = this.CreateSpatialMapper(stripContainer);
            this.dragContext = new DragContext(source, item, hotspotOffsets, stripContainer, draggedElement, mapper);
            var initialPhysicalPosition = mapper.ToPhysicalScreen(initialPosition);
            this.lastCursorPosition = initialPhysicalPosition;
            this.isActive = true;
            this.SwitchToReorderMode(initialPhysicalPosition);

            this.StartPollingTimer();
            this.LogDragStarted();
        }
    }

    /// <summary>
    ///     Ends the current drag operation and finalizes the drop.
    /// </summary>
    /// <param name="screenPoint">Screen coordinate where the drop occurred.</param>
    public async void EndDrag(SpatialPoint<ScreenSpace> screenPoint)
    {
        if (!this.TryCaptureEndDragState(screenPoint, out var context, out var hitStrip, out var isTearOutMode))
        {
            return;
        }

        // If there's a hit strip and we're NOT in tear-out mode, prepare for drop. If we're in
        // tear-out mode at button release, treat it as a tear-out completion (don't reattach to
        // strips that happen to be under the cursor).
        var fallbackDropIndex = -1;
        if (hitStrip is not null && !isTearOutMode)
        {
            var result = await this.PrepareDropForHitStrip(screenPoint, hitStrip, isTearOutMode).ConfigureAwait(true);
            fallbackDropIndex = result.insertionIndex;
            isTearOutMode = result.isTearOutMode;
        }

        if (fallbackDropIndex < 0)
        {
            fallbackDropIndex = GetDraggedItemIndex(context!);
        }

        this.LogEndDragPreparation(isTearOutMode, hitStrip, fallbackDropIndex, context!);

        // Complete and finalize the end-drag under lock.
        this.CompleteEndDrag(context!, hitStrip, isTearOutMode, fallbackDropIndex, screenPoint);
    }

    /// <summary>
    ///     Registers a TabStrip instance for cross-window drag coordination.
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
    ///     Unregisters a TabStrip instance from cross-window drag coordination.
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
    ///     Aborts an active drag, restoring state and ending the visual session.
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

    private static int GetDraggedItemIndex(DragContext context)
        => context.TabStrip is null ? -1 : context.TabStrip.IndexOf(context.DraggedItemData);

    private bool TryCaptureEndDragState(SpatialPoint<ScreenSpace> screenPoint, out DragContext? context, out ITabStrip? hitStrip, out bool isTearOutMode)
    {
        context = null;
        hitStrip = null;
        isTearOutMode = false;

        lock (this.syncLock)
        {
            if (!this.isActive || this.dragContext is null)
            {
                this.LogDragEndedIgnored();
                return false;
            }

            this.StopPollingTimer();

            context = this.dragContext;
            Debug.Assert(context is not null, "Drag context should not be null inside EndDrag");

            hitStrip = this.GetHitTestTabStrip(screenPoint);
            isTearOutMode = this.currentStrategy == this.tearOutStrategy;
        }

        return true;
    }

    private void CompleteEndDrag(DragContext context, ITabStrip? hitStrip, bool isTearOutMode, int fallbackDropIndex, SpatialPoint<ScreenSpace> screenPoint)
    {
        lock (this.syncLock)
        {
            // Complete the current strategy (strategy will handle the drop)
            int? finalDropIndex = null;
            if (this.currentStrategy is not null)
            {
                finalDropIndex = this.currentStrategy.CompleteDrag(drop: true);
                this.LogStrategyCompletion(this.currentStrategy, finalDropIndex);
            }

            if (!finalDropIndex.HasValue && hitStrip is not null
                && ReferenceEquals(context.TabStrip, hitStrip) && fallbackDropIndex >= 0)
            {
                finalDropIndex = fallbackDropIndex;
                this.LogFallbackIndex(fallbackDropIndex);
            }

            this.FinalizeDropOutcome(context, isTearOutMode, hitStrip, finalDropIndex, screenPoint);
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

    private bool TryCreateMapperForStrip(ITabStrip strip, out ISpatialMapper mapper, out FrameworkElement container)
    {
        container = strip.GetContainerElement();

        try
        {
            mapper = this.CreateSpatialMapper(container);
            return true;
        }
        catch (InvalidOperationException ex)
        {
            this.LogSpatialMapperCreationFailed(strip, ex);
            mapper = default!;
            return false;
        }
    }

    private (ITabStrip strip, ISpatialMapper mapper, FrameworkElement container)? FindStripUnderCursor(SpatialPoint<PhysicalScreenSpace> cursor)
    {
        lock (this.syncLock)
        {
            _ = this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            foreach (var weakRef in this.registeredStrips)
            {
                if (!weakRef.TryGetTarget(out var strip))
                {
                    continue;
                }

                if (!this.TryCreateMapperForStrip(strip, out var mapper, out var container))
                {
                    continue;
                }

                var elementPoint = mapper.Convert<PhysicalScreenSpace, ElementSpace>(cursor);
                if (strip.HitTestWithThreshold(elementPoint, DragThresholds.TearOutThreshold) >= 0)
                {
                    return (strip, mapper, container);
                }
            }
        }

        return null;
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
    /// Performs hit-testing to find which TabStrip (if any) is under the cursor.
    /// </summary>
    /// <param name="screenCursor">Cursor position in screen coordinates.</param>
    /// <returns>TabStrip under cursor, or null if none found.</returns>
    private ITabStrip? GetHitTestTabStrip(SpatialPoint<ScreenSpace> screenCursor)
    {
        lock (this.syncLock)
        {
            _ = this.registeredStrips.RemoveAll(wr => !wr.TryGetTarget(out _));

            foreach (var weakRef in this.registeredStrips)
            {
                if (!weakRef.TryGetTarget(out var strip))
                {
                    continue;
                }

                if (!this.TryCreateMapperForStrip(strip, out var mapper, out _))
                {
                    continue;
                }

                var elementPoint = mapper.Convert<ScreenSpace, ElementSpace>(screenCursor);
                if (strip.HitTestWithThreshold(elementPoint, DragThresholds.TearOutThreshold) >= 0)
                {
                    return strip;
                }
            }

            return null;
        }
    }

    private async Task<(int insertionIndex, bool isTearOutMode)> PrepareDropForHitStrip(
        SpatialPoint<ScreenSpace> screenPoint,
        ITabStrip hitStrip,
        bool isTearOutMode)
    {
        if (this.dragContext is null)
        {
            return (-1, isTearOutMode);
        }

        if (!this.TryCreateMapperForStrip(hitStrip, out var mapper, out var container))
        {
            return (GetDraggedItemIndex(this.dragContext), isTearOutMode);
        }

        var physicalPoint = mapper.Convert<ScreenSpace, PhysicalScreenSpace>(screenPoint);

        if (isTearOutMode)
        {
            var ready = await this.AttachToStrip(hitStrip, container, mapper, physicalPoint).ConfigureAwait(true);

            if (ready)
            {
                this.SwitchToReorderMode(physicalPoint);
                isTearOutMode = false;
            }
        }

        return (GetDraggedItemIndex(this.dragContext), isTearOutMode);
    }

    private void SwitchToReorderMode(SpatialPoint<PhysicalScreenSpace> position)
    {
        if (this.dragContext is null)
        {
            return;
        }

        if (this.currentStrategy is not null)
        {
            _ = this.currentStrategy.CompleteDrag(drop: false);
        }

        this.currentStrategy = this.reorderStrategy;
        this.lastCursorPosition = position;
        this.currentStrategy.InitiateDrag(this.dragContext, position);
    }

    private void SwitchToTearOutMode(SpatialPoint<PhysicalScreenSpace> position)
    {
        if (this.dragContext is null)
        {
            return;
        }

        if (this.currentStrategy is not null)
        {
            _ = this.currentStrategy.CompleteDrag(drop: false);
        }

        var context = this.dragContext;
        var currentStrip = context.TabStrip;

        if (currentStrip is not null)
        {
            this.LogTearOutStart(context.DraggedItemData.ToString() ?? "Unknown", GetDraggedItemIndex(context), currentStrip.Name);
            try
            {
                // DetachTab raises TabDetachRequested. This signals the application to detach
                // the document for tear-out and is not a close operation. The application is
                // responsible for detaching the item in response; the UI should not interpret
                // this as a request to close the document.
                currentStrip.DetachTab(context.DraggedItemData);
                this.LogTearOutDetachTab(context.DraggedItemData.ToString() ?? "Unknown");
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                this.LogTearOutFailed(ex);
                currentStrip.TryCompleteDrag(
                    context.DraggedItemData,
                    currentStrip,
                    GetDraggedItemIndex(context));
                this.CleanupState();
                return;
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        context.UpdateCurrentStrip(tabStrip: null, stripContainer: null, context.SpatialMapper);
        this.LogTearOutContextReset(context.DraggedItemData.ToString() ?? "Unknown");

        this.currentStrategy = this.tearOutStrategy;
        this.lastCursorPosition = position;
        this.currentStrategy.InitiateDrag(context, position);
    }

    private void StartPollingTimer()
    {
        this.pollingTimer = this.dispatcherQueue.CreateTimer();
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

    private async void OnPollingTimerTick(DispatcherQueueTimer sender, object args)
    {
        SpatialPoint<PhysicalScreenSpace> newPosition = default;
        SpatialPoint<PhysicalScreenSpace> previousPosition = default;
        var shouldProcess = false;

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

            var candidate = new Windows.Foundation.Point(cursorPos.X, cursorPos.Y).AsPhysicalScreen();

            // Check if position changed (compare physical pixels)
            if (Math.Abs(candidate.Point.X - this.lastCursorPosition.Point.X) > 0.5
                || Math.Abs(candidate.Point.Y - this.lastCursorPosition.Point.Y) > 0.5)
            {
                previousPosition = this.lastCursorPosition;
                this.lastCursorPosition = candidate;
                newPosition = candidate;
                shouldProcess = true;
            }
        }

        if (!shouldProcess)
        {
            return;
        }

        // Handle the actual moved logic (check mode transitions then update strategy)
        await this.HandleCursorMovedAsync(newPosition, previousPosition).ConfigureAwait(true);
    }

    private async Task HandleCursorMovedAsync(SpatialPoint<PhysicalScreenSpace> newPosition, SpatialPoint<PhysicalScreenSpace> previousPosition)
    {
        // Check for mode transitions based on cursor position (may await)
        await this.CheckAndHandleModeTransitions(newPosition).ConfigureAwait(true);

        // Delegate to current strategy (pass screen coordinates, strategy will convert)
        this.DelegateToCurrentStrategy(newPosition, previousPosition);
    }

    private void DelegateToCurrentStrategy(SpatialPoint<PhysicalScreenSpace> newPosition, SpatialPoint<PhysicalScreenSpace> previousPosition)
    {
        lock (this.syncLock)
        {
            if (this.dragContext is not null && this.currentStrategy is not null)
            {
                var strategy = this.currentStrategy!;

                // Determine the best dispatcher to use for UI work. If the drag context
                // is associated with a TabStrip and its container exposes a DispatcherQueue,
                // prefer that — this ensures UI work runs on the actual TabStrip's window
                // dispatcher in multi-window scenarios. Otherwise, fallback to the
                // coordinator's dispatcher queue.
                var targetDispatcher = this.dragContext.TabStripContainer?.DispatcherQueue ?? this.dispatcherQueue;
                var logged = false;

                if (targetDispatcher.HasThreadAccess)
                {
                    strategy.OnDragPositionChanged(newPosition);
                    this.LogDragMoved(newPosition, previousPosition.Point);
                    logged = true;
                }
                else
                {
                    var enqueued = targetDispatcher.TryEnqueue(() =>
                    {
                        try
                        {
                            strategy.OnDragPositionChanged(newPosition);
                            this.LogDragMoved(newPosition, previousPosition.Point);
                        }
#pragma warning disable CA1031 // Do not catch general exception types
                        catch (Exception ex)
                        {
                            this.LogOnDragPositionChangedError(ex);
                        }
#pragma warning restore CA1031 // Do not catch general exception types
                    });

                    if (!enqueued)
                    {
                        // Use general dispatcher enqueue failed logging with a specific
                        // operation name so the log message clearly indicates what failed.
                        this.LogDispatcherEnqueueFailed("OnDragPositionChanged");
                    }

                    // We won't call LogDragMoved here (it will be logged inside the enqueued action)
                    logged = true;
                }

                if (!logged)
                {
                    // If, for some reason, nothing logged we log here as a last resort.
                    this.LogDragMoved(newPosition, previousPosition.Point);
                }
            }
        }
    }

    private async Task CheckAndHandleModeTransitions(SpatialPoint<PhysicalScreenSpace> cursor)
    {
        if (this.dragContext is null)
        {
            return;
        }

        if (this.currentStrategy == this.reorderStrategy)
        {
            await this.HandleReorderModeTransitions(cursor).ConfigureAwait(true);
            return;
        }

        if (this.currentStrategy != this.tearOutStrategy)
        {
            return;
        }

        await this.HandleTearOutModeTransitions(cursor).ConfigureAwait(true);
    }

    private Task HandleReorderModeTransitions(SpatialPoint<PhysicalScreenSpace> cursor)
    {
        if (this.dragContext is null)
        {
            return Task.CompletedTask;
        }

        if (this.dragContext.TabStrip is not { } activeStrip)
        {
            this.SwitchToTearOutMode(cursor);
            return Task.CompletedTask;
        }

        var elementPoint = this.dragContext.SpatialMapper.ToElement(cursor);
        var hitTest = activeStrip.HitTestWithThreshold(elementPoint, DragThresholds.TearOutThreshold);

        if (hitTest < 0)
        {
            this.SwitchToTearOutMode(cursor);
        }

        return Task.CompletedTask;
    }

    private async Task HandleTearOutModeTransitions(SpatialPoint<PhysicalScreenSpace> cursor)
    {
        // Prevent concurrent attachment attempts
        lock (this.syncLock)
        {
            if (this.isAttaching)
            {
                return;
            }

            this.isAttaching = true;
        }

        try
        {
            var hitResult = this.FindStripUnderCursor(cursor);
            if (hitResult is null)
            {
                return;
            }

            var (targetStrip, mapper, container) = hitResult.Value;

            var readyForReorder = await this.AttachToStrip(targetStrip, container, mapper, cursor).ConfigureAwait(true);
            if (!readyForReorder)
            {
                return;
            }

            this.SwitchToReorderMode(cursor);
        }
        finally
        {
            lock (this.syncLock)
            {
                this.isAttaching = false;
            }
        }
    }

    private async Task<bool> AttachToStrip(ITabStrip targetStrip, FrameworkElement container, ISpatialMapper mapper, SpatialPoint<PhysicalScreenSpace> cursor)
    {
        if (this.dragContext is null)
        {
            return false;
        }

        this.DetachIfSwitchingStrips(targetStrip);

        var elementPoint = mapper.Convert<PhysicalScreenSpace, ElementSpace>(cursor);

        var payloadClone = this.dragContext.DraggedItemData.ShallowClone();
        var contentId = payloadClone.ContentId;

        var cts = new CancellationTokenSource();
        lock (this.syncLock)
        {
            this.pendingInsertCts[contentId] = cts;
        }

        ExternalDropPreparationResult? preparation = null;
        try
        {
            preparation = await this.EnqueuePrepareExternalDropAsync(container, targetStrip, payloadClone, elementPoint, cts.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            this.LogAttachToStripCancelled();
            return false;
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogAttachToStripFailed(ex);
            return false;
        }
#pragma warning restore CA1031 // Do not catch general exception types
        finally
        {
            lock (this.syncLock)
            {
                if (this.pendingInsertCts.TryGetValue(contentId, out var existingCts))
                {
                    existingCts.Dispose();
                    _ = this.pendingInsertCts.Remove(contentId);
                }
            }

            cts.Dispose();
        }

        if (preparation is null)
        {
            return false;
        }

        this.dragContext.UpdateCurrentStrip(targetStrip, container, mapper);
        this.dragContext.UpdateDraggedVisualElement(preparation.RealizedContainer);
        this.LogAttachToStripSucceeded(targetStrip.Name, preparation.DropIndex);
        return true;
    }

    private async Task<ExternalDropPreparationResult?> EnqueuePrepareExternalDropAsync(
        FrameworkElement container,
        ITabStrip targetStrip,
        IDragPayload payloadClone,
        SpatialPoint<ElementSpace> elementPoint,
        CancellationToken ct)
    {
        if (container.DispatcherQueue is null)
        {
            this.LogAttachToStripAborted();
            return null;
        }

        try
        {
            // Use CommunityToolkit's EnqueueAsync helper to marshal the call to the container's dispatcher.
            return await container.DispatcherQueue
                .EnqueueAsync(() => targetStrip.PrepareExternalDropAsync(payloadClone, elementPoint, ct))
                .ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            // If scheduling fails, mimic original behavior by logging and returning null
            // so the caller can handle failure.
            this.LogDispatcherEnqueueFailed("AttachToStrip");
            return null;
        }
    }

    private void DetachIfSwitchingStrips(ITabStrip targetStrip)
    {
        if (this.dragContext?.TabStrip is { } pendingStrip && !ReferenceEquals(pendingStrip, targetStrip))
        {
            this.DetachPendingAttachment(pendingStrip);
        }
    }

    private void DetachPendingAttachment(ITabStrip pendingStrip)
    {
        this.LogDetachPendingAttachment(
            pendingStrip.Name,
            this.dragContext is not null ? GetDraggedItemIndex(this.dragContext) : null);

        if (this.dragContext is null)
        {
            return;
        }

        // Instead of directly removing the pending clone from the control (which can race with
        // control-side cleanup), signal cancellation for the pending insert operation if we
        // previously started one for this payload. The TabStrip control owns removing the
        // pending clone on cancellation/timeouts (see TabStrip.InsertItemAsync).
        var cid = this.dragContext.DraggedItemData.ContentId;
        if (this.pendingInsertCts.TryGetValue(cid, out var cts))
        {
            try
            {
                cts.Cancel();
                this.LogCancelledPendingInsert(cid);
            }
            finally
            {
                cts.Dispose();
                _ = this.pendingInsertCts.Remove(cid);
            }
        }

        if (this.dragContext.TabStripContainer is { } pendingContainer)
        {
            // Dispatch a defensive layout update; do not block here. If dispatch fails
            // we ignore it — the control is responsible for eventual cleanup.
            _ = pendingContainer.DispatcherQueue.EnqueueAsync(pendingContainer.UpdateLayout);
        }

        this.dragContext.UpdateCurrentStrip(tabStrip: null, stripContainer: null, this.dragContext.SpatialMapper);
        this.dragContext.UpdateDraggedVisualElement(this.dragContext.DraggedVisualElement);
    }

    private void FinalizeDropOutcome(
        DragContext context,
        bool isTearOutMode,
        ITabStrip? hitStrip,
        int? finalDropIndex,
        SpatialPoint<ScreenSpace> screenPoint)
    {
        var sourceStrip = context.SourceTabStrip;

        if (isTearOutMode && hitStrip is null && sourceStrip is not null)
        {
            sourceStrip.TearOutTab(context.DraggedItemData, screenPoint);
            sourceStrip.TryCompleteDrag(context.DraggedItemData, destinationStrip: null, newIndex: null);
        }
        else if (hitStrip is not null && finalDropIndex.HasValue)
        {
            hitStrip.TryCompleteDrag(context.DraggedItemData, hitStrip, finalDropIndex.Value);
        }
        else
        {
            sourceStrip?.TryCompleteDrag(context.DraggedItemData, destinationStrip: null, newIndex: null);
        }

        this.LogDragEnded(screenPoint, hitStrip is not null, hitStrip, finalDropIndex);
    }

    private void CleanupState()
    {
        this.isActive = false;
        this.dragContext = null;
        this.currentStrategy = null;
        this.lastCursorPosition = default;
        this.isAttaching = false;
    }
}
