// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
    private bool isActive;
    private TabItem? draggedItem;
    private TabStrip? sourceStrip;
    private DragSessionToken? sessionToken;
    private DragVisualDescriptor? descriptor;
    private DispatcherQueueTimer? pollingTimer;
    private Windows.Foundation.Point lastCursorPosition;
    private Windows.Foundation.Point hotspot;

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
    /// <param name="visualDescriptor">Descriptor describing the drag visual overlay.</param>
    /// <param name="hotspot">Hotspot offset from the top-left of the overlay where the cursor should align.</param>
    public void StartDrag(
        TabItem item,
        TabStrip source,
        DragVisualDescriptor visualDescriptor,
        Windows.Foundation.Point hotspot)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(visualDescriptor);

        lock (this.syncLock)
        {
            if (this.isActive)
            {
                this.LogDragAlreadyActive();
                throw new InvalidOperationException("A drag is already active in this process.");
            }

            this.isActive = true;
            this.draggedItem = item;
            this.sourceStrip = source;
            this.descriptor = visualDescriptor;
            this.hotspot = hotspot;

            // Start the visual session via the service
            this.sessionToken = this.dragService.StartSession(visualDescriptor, hotspot);

            // Get initial cursor position (GetCursorPos returns physical pixels)
            if (Native.GetCursorPos(out var cursorPos))
            {
                // Store physical position for tracking
                this.lastCursorPosition = new Windows.Foundation.Point(cursorPos.X, cursorPos.Y);

                // Pass physical pixels directly to service (service handles DPI conversion)
                this.dragService.UpdatePosition(this.sessionToken.Value, this.lastCursorPosition);
            }

            // Start polling timer at 60Hz (approximately every 16.67ms)
            this.StartPollingTimer();

            this.LogDragStarted();
        }
    }

    /// <summary>
    /// Moves the current drag visual to the given screen coordinates. The coordinator will
    /// notify the source/target TabStrip via internal hooks when appropriate (TabStrip integration
    /// occurs in Phase 3).
    /// </summary>
    /// <param name="screenPoint">Screen coordinate of the pointer during drag.</param>
    public void Move(Windows.Foundation.Point screenPoint)
    {
        lock (this.syncLock)
        {
            if (!this.isActive || !this.sessionToken.HasValue)
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

            // Update overlay position
            this.dragService.UpdatePosition(this.sessionToken.Value, screenPoint);

            this.LogDragMoved(screenPoint, previousPosition);

            // Raise DragMoved event for subscribers (TabStrip instances will subscribe in Phase 3).
            this.DragMoved?.Invoke(this, new DragMovedEventArgs { ScreenPoint = screenPoint });
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
            if (!this.isActive)
            {
                this.LogDragEndedIgnored();
                return;
            }

            // Stop polling timer
            this.StopPollingTimer();

            this.LogDragEnded(screenPoint, droppedOverStrip, destination, newIndex);

            // Notify subscribers that the drag ended so they can finalize insertion/removal.
            this.DragEnded?.Invoke(
                this,
                new DragEndedEventArgs
                {
                    ScreenPoint = screenPoint,
                    DroppedOverStrip = droppedOverStrip,
                    Destination = destination,
                    NewIndex = newIndex,
                });

            // End visual session
            if (this.sessionToken.HasValue)
            {
                this.dragService.EndSession(this.sessionToken.Value);
            }

            // Clear state
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

            // Stop polling timer
            this.StopPollingTimer();

            this.LogDragAborted();

            if (this.sessionToken.HasValue)
            {
                this.dragService.EndSession(this.sessionToken.Value);
            }

            this.CleanupState();
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
            if (!this.isActive || !this.sessionToken.HasValue)
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

                // Pass physical pixels directly to service (service handles DPI conversion)
                this.dragService.UpdatePosition(this.sessionToken.Value, newPosition);

                this.LogDragMoved(newPosition, previousPosition);

                // Raise DragMoved event (consumers get physical screen coordinates)
                this.DragMoved?.Invoke(this, new DragMovedEventArgs { ScreenPoint = newPosition });
            }
        }
    }

    private void CleanupState()
    {
        this.isActive = false;
        this.draggedItem = null;
        this.sourceStrip = null;
        this.sessionToken = null;
        this.descriptor = null;
        this.hotspot = default;
        this.lastCursorPosition = default;
    }
}
