// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Controls;
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
    private readonly Dictionary<Guid, CancellationTokenSource> pendingInsertCts = new();

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
    /// Starts a drag operation for the provided <paramref name="item"/> originating from <paramref name="source"/>.
    /// </summary>
    /// <param name="item">Logical TabItem being dragged.</param>
    /// <param name="itemIndex">The index of the item being dragged within the source TabStrip.</param>
    /// <param name="source">Source TabStrip.</param>
    /// <param name="stripContainer">The container element that hosts the source <see cref="ITabStrip"/>.</param>
    /// <param name="draggedElement">Visual element for drag preview rendering.</param>
    /// <param name="initialPosition">Initial pointer position relative to the source element.</param>
    /// <param name="hotspotOffsets">Offset from the drag hotspot to the dragged element's origin.</param>
    public void StartDrag(
        object item,
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

            this.isActive = true;

            var mapper = this.CreateSpatialMapper(stripContainer);
            this.dragContext = new DragContext(source, item, itemIndex, hotspotOffsets, stripContainer, draggedElement, mapper);
            var initialPhysicalPosition = mapper.ToPhysicalScreen(initialPosition);
            this.lastCursorPosition = initialPhysicalPosition;
            this.SwitchToReorderMode(initialPhysicalPosition);

            this.StartPollingTimer();
            this.LogDragStarted();
        }
    }

    /// <summary>
    /// Ends the current drag operation.
    /// </summary>
    /// <param name="screenPoint">Screen coordinate where the drop occurred.</param>
    public async void EndDrag(SpatialPoint<ScreenSpace> screenPoint)
    {
        DragContext? context;
        ITabStrip? hitStrip;
        bool isTearOutMode;

        // Capture necessary state under lock, then release before awaiting UI operations.
        lock (this.syncLock)
        {
            if (!this.isActive || this.dragContext is null)
            {
                this.LogDragEndedIgnored();
                return;
            }

            this.StopPollingTimer();

            context = this.dragContext;
            Debug.Assert(context is not null, "Drag context should not be null inside EndDrag");

            hitStrip = this.GetHitTestTabStrip(screenPoint);
            isTearOutMode = this.currentStrategy == this.tearOutStrategy;
        }

        // If there's a hit strip, ask it to prepare for drop (may await realization handshake).
        int fallbackDropIndex;
        if (hitStrip is not null)
        {
            var result = await this.PrepareDropForHitStrip(screenPoint, hitStrip, isTearOutMode);
            fallbackDropIndex = result.insertionIndex;
            isTearOutMode = result.isTearOutMode;
        }
        else
        {
            fallbackDropIndex = context!.DraggedItemIndex;
        }

        if (fallbackDropIndex < 0)
        {
            fallbackDropIndex = context!.DraggedItemIndex;
        }

        this.LogEndDragPreparation(isTearOutMode, hitStrip, fallbackDropIndex, context!);

        // Re-enter critical section to complete the strategy and finalize outcome.
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
                && ReferenceEquals(context!.TabStrip, hitStrip) && fallbackDropIndex >= 0)
            {
                finalDropIndex = fallbackDropIndex;
                this.LogFallbackIndex(fallbackDropIndex);
            }

            this.FinalizeDropOutcome(context!, isTearOutMode, hitStrip, finalDropIndex, screenPoint);

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

    private bool TryCreateMapperForStrip(ITabStrip strip, out ISpatialMapper mapper, out FrameworkElement? container)
    {
        container = strip as FrameworkElement;
        if (container is null)
        {
            this.LogStripNotFrameworkElement(strip);
            mapper = default!;
            return false;
        }

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

                if (!this.TryCreateMapperForStrip(strip, out var mapper, out var container) || container is null)
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

        if (!this.TryCreateMapperForStrip(hitStrip, out var mapper, out var container) || container is null)
        {
            return (this.dragContext.DraggedItemIndex, isTearOutMode);
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

        return (this.dragContext.DraggedItemIndex, isTearOutMode);
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
            Debug.WriteLine($"[TabDragCoordinator] Tear-out start: item='{context.DraggedItemData}', index={context.DraggedItemIndex}, strip='{currentStrip.Name}'");
            try
            {
                currentStrip.CloseTab(context.DraggedItemData);
                Debug.WriteLine($"[TabDragCoordinator] Tear-out CloseTab issued for item='{context.DraggedItemData}'");

                if (context.DraggedItemIndex >= 0)
                {
                    try
                    {
                        currentStrip.RemoveItemAt(context.DraggedItemIndex);
                        Debug.WriteLine($"[TabDragCoordinator] Tear-out RemoveItemAt({context.DraggedItemIndex}) succeeded");
                    }
                    catch (ArgumentOutOfRangeException)
                    {
                        // Item already removed externally; ignore and continue tear-out.
                        Debug.WriteLine($"[TabDragCoordinator] Tear-out RemoveItemAt skipped because index {context.DraggedItemIndex} is out of range");
                    }
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                this.LogTabCloseRequestedFailed(ex);
                Debug.WriteLine($"[TabDragCoordinator] Tear-out failed: {ex}");
                currentStrip.TryCompleteDrag(
                    context.DraggedItemData,
                    currentStrip,
                    context.DraggedItemIndex);
                this.CleanupState();
                return;
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }

        context.UpdateCurrentStrip(null, null, context.SpatialMapper, -1);
        Debug.WriteLine($"[TabDragCoordinator] Tear-out context reset for item='{context.DraggedItemData}'");

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

        // Check for mode transitions based on cursor position (may await)
        await this.CheckAndHandleModeTransitions(newPosition);

        // Delegate to current strategy (pass screen coordinates, strategy will convert)
        lock (this.syncLock)
        {
            if (this.dragContext is not null && this.currentStrategy is not null)
            {
                var strategy = this.currentStrategy!;
                try
                {
                    Debug.WriteLine($"[TabDragCoordinator] Invoking OnDragPositionChanged: cursor={newPosition.Point}, lastCursor={previousPosition.Point}, isTearOut={(strategy == this.tearOutStrategy)}, strategy={strategy.GetType().Name}");

                    // Ensure we execute UI-manipulating code on the UI's DispatcherQueue
                    // to avoid RPC_E_WRONG_THREAD and native runtime races when the
                    // continuation after awaits executes on a thread-pool thread.
                    var logged = false;

                    if (this.dispatcherQueue.HasThreadAccess)
                    {
                        this.logger.LogDebug("[TabDragCoordinator] OnDragPositionChanged executing on UI thread (tid={ThreadId})", Thread.CurrentThread.ManagedThreadId);
                        strategy.OnDragPositionChanged(newPosition);
                        this.LogDragMoved(newPosition, previousPosition.Point);
                        logged = true;
                    }
                    else
                    {
                        this.logger.LogDebug("[TabDragCoordinator] Enqueueing OnDragPositionChanged to DispatcherQueue (tid={ThreadId})", Thread.CurrentThread.ManagedThreadId);
                        var enqueued = dispatcherQueue.TryEnqueue(() =>
                        {
                            try
                            {
                                this.logger.LogDebug("[TabDragCoordinator] Enqueued OnDragPositionChanged running on UI thread (tid={ThreadId})", Thread.CurrentThread.ManagedThreadId);
                                strategy.OnDragPositionChanged(newPosition);
                                this.LogDragMoved(newPosition, previousPosition.Point);
                            }
                            catch (Exception ex)
                            {
                                this.logger.LogError(ex, "[TabDragCoordinator] Exception in enqueued OnDragPositionChanged");
                            }
                        });

                        if (!enqueued)
                        {
                            this.logger.LogWarning("[TabDragCoordinator] Failed to enqueue OnDragPositionChanged to UI dispatcher (tid={ThreadId})", Thread.CurrentThread.ManagedThreadId);
                        }

                        // We won't call LogDragMoved here (it will be logged inside the enqueued action)
                        logged = true;
                    }

                    if (!logged)
                    {
                        // If we didn't already log via the dispatched path, log movement now.
                        this.LogDragMoved(newPosition, previousPosition.Point);
                    }
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
                {
                    // Log and swallow to prevent unhandled managed exceptions from
                    // propagating into native XAML tooling where they can surface as
                    // access violations. We intentionally swallow here but record
                    // diagnostic details for investigation.
                    this.logger.LogError(ex, "[TabDragCoordinator] Exception in OnDragPositionChanged (tid={ThreadId})", Thread.CurrentThread.ManagedThreadId);
                }
#pragma warning restore CA1031 // Do not catch general exception types
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
            if (this.dragContext.TabStrip is not { } activeStrip)
            {
                this.SwitchToTearOutMode(cursor);
                return;
            }

            var elementPoint = this.dragContext.SpatialMapper.Convert<PhysicalScreenSpace, ElementSpace>(cursor);
            var hitTest = activeStrip.HitTestWithThreshold(elementPoint, DragThresholds.TearOutThreshold);

            if (hitTest < 0)
            {
                this.SwitchToTearOutMode(cursor);
            }

            return;
        }

        if (this.currentStrategy != this.tearOutStrategy)
        {
            return;
        }

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

    private async Task<bool> AttachToStrip(ITabStrip targetStrip, FrameworkElement container, ISpatialMapper mapper, SpatialPoint<PhysicalScreenSpace> cursor)
    {
        if (this.dragContext is null)
        {
            return false;
        }

        this.DetachIfSwitchingStrips(targetStrip);

        var elementPoint = mapper.Convert<PhysicalScreenSpace, ElementSpace>(cursor);

        object payloadClone = this.dragContext.DraggedItemData;
        if (this.dragContext.DraggedItemData is IDragPayload payload)
        {
            payloadClone = payload.ShallowClone();
        }

        Guid? contentId = (payloadClone as IDragPayload)?.ContentId;
        var cts = new CancellationTokenSource();
        if (contentId.HasValue)
        {
            lock (this.syncLock)
            {
                this.pendingInsertCts[contentId.Value] = cts;
            }
        }

        ExternalDropPreparationResult? preparation = null;
        try
        {
            if (container.DispatcherQueue is null)
            {
                Debug.WriteLine("[TabDragCoordinator] AttachToStrip aborted: container has no DispatcherQueue.");
                return false;
            }

            if (container.DispatcherQueue.HasThreadAccess)
            {
                preparation = await targetStrip.PrepareExternalDropAsync(payloadClone, elementPoint, cts.Token).ConfigureAwait(true);
            }
            else
            {
                var tcs = new TaskCompletionSource<ExternalDropPreparationResult?>(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!container.DispatcherQueue.TryEnqueue(async () =>
                {
                    try
                    {
                        var result = await targetStrip.PrepareExternalDropAsync(payloadClone, elementPoint, cts.Token).ConfigureAwait(true);
                        tcs.TrySetResult(result);
                    }
                    catch (Exception ex)
                    {
                        tcs.TrySetException(ex);
                    }
                }))
                {
                    Debug.WriteLine("[TabDragCoordinator] AttachToStrip failed to enqueue preparation on UI thread.");
                    return false;
                }

                preparation = await tcs.Task.ConfigureAwait(true);
            }
        }
        catch (OperationCanceledException)
        {
            Debug.WriteLine("[TabDragCoordinator] AttachToStrip cancelled during preparation.");
            return false;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[TabDragCoordinator] AttachToStrip failed: {ex}");
            return false;
        }
        finally
        {
            if (contentId.HasValue)
            {
                lock (this.syncLock)
                {
                    if (this.pendingInsertCts.TryGetValue(contentId.Value, out var existingCts))
                    {
                        existingCts.Dispose();
                        _ = this.pendingInsertCts.Remove(contentId.Value);
                    }
                }
            }
            else
            {
                cts.Dispose();
            }
        }

        if (preparation is null)
        {
            return false;
        }

        this.dragContext.UpdateCurrentStrip(targetStrip, container, mapper, preparation.DropIndex);
        this.dragContext.UpdateDraggedVisualElement(preparation.RealizedContainer);
        Debug.WriteLine($"[TabDragCoordinator] AttachToStrip succeeded: strip='{targetStrip.Name}', index={preparation.DropIndex}");
        return true;
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
        Debug.WriteLine($"[TabDragCoordinator] Detaching pending attachment from strip='{pendingStrip.Name}', index={this.dragContext?.DraggedItemIndex}");

        if (this.dragContext is null)
        {
            return;
        }

        // Instead of directly removing the pending clone from the control (which can race with
        // control-side cleanup), signal cancellation for the pending insert operation if we
        // previously started one for this payload. The TabStrip control owns removing the
        // pending clone on cancellation/timeouts (see TabStrip.InsertItemAsync).
        if (this.dragContext.DraggedItemData is IDragPayload payload)
        {
            var cid = payload.ContentId;
            if (this.pendingInsertCts.TryGetValue(cid, out var cts))
            {
                try
                {
                    cts.Cancel();
                    Debug.WriteLine($"[TabDragCoordinator] Cancelled pending insert for ContentId={cid}");
                }
                finally
                {
                    cts.Dispose();
                    _ = this.pendingInsertCts.Remove(cid);
                }
            }
        }

        if (this.dragContext.TabStripContainer is { } pendingContainer)
        {
            // Dispatch a defensive layout update; do not block here. If dispatch fails
            // we ignore it — the control is responsible for eventual cleanup.
            _ = pendingContainer.DispatcherQueue.EnqueueAsync(() => pendingContainer.UpdateLayout());
        }

        this.dragContext.UpdateCurrentStrip(null, null, this.dragContext.SpatialMapper, -1);
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
    }
}
