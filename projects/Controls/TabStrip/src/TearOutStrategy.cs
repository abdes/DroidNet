// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;

namespace DroidNet.Controls;

/// <summary>
///     Strategy for handling cross-window drag operations using DragVisualService overlay.
///     This strategy manages TearOut mode with floating overlay visuals and cross-window coordination.
/// </summary>
internal sealed partial class TearOutStrategy : IDragStrategy
{
    private readonly IDragVisualService dragService;
    private readonly TabDragCoordinator coordinator;
    private readonly ILogger logger;

    private bool isActive;
    private DragContext? context;
    private DragSessionToken? sessionToken;
    private DragVisualDescriptor? descriptor;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TearOutStrategy"/> class.
    /// </summary>
    /// <param name="dragService">The drag visual service for managing overlay.</param>
    /// <param name="coordinator">The coordinator that owns this strategy.</param>
    /// <param name="loggerFactory">The logger factory for creating loggers.</param>
    public TearOutStrategy(IDragVisualService dragService, TabDragCoordinator coordinator, ILoggerFactory? loggerFactory = null)
    {
        this.dragService = dragService ?? throw new ArgumentNullException(nameof(dragService));
        this.coordinator = coordinator ?? throw new ArgumentNullException(nameof(coordinator));
        this.logger = loggerFactory?.CreateLogger<TearOutStrategy>() ?? NullLogger<TearOutStrategy>.Instance;
        this.LogCreated();
    }

    /// <inheritdoc/>
    public bool IsActive => this.isActive;

    /// <inheritdoc/>
    public void InitiateDrag(DragContext context, SpatialPoint<ScreenSpace> initialPoint)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (this.isActive)
        {
            this.LogAlreadyActive();
            throw new InvalidOperationException("TearOutStrategy is already active");
        }

        this.context = context;
        this.isActive = true;

        this.LogEnterTearOutMode(initialPoint.Point);

        // Capture header image from the source visual item
        var headerImage = this.CaptureHeaderImage();

        // Create descriptor for the drag visual
        this.descriptor = new DragVisualDescriptor
        {
            HeaderImage = headerImage,
            RequestedSize = new Windows.Foundation.Size(300, 150), // Default size
            Title = context.DraggedItem.Header,
        };

        // Request preview image from the application
        this.RequestPreviewImage();

        // Start the drag visual session with the correct logical hotspot from context
        this.sessionToken = this.dragService.StartSession(this.descriptor, initialPoint.Point);

        // Update initial position
        this.dragService.UpdatePosition(this.sessionToken.Value, initialPoint.Point);
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint<ScreenSpace> currentPoint)
    {
        if (!this.isActive || !this.sessionToken.HasValue)
        {
            this.LogMoveIgnored();
            return;
        }

        // Update overlay position
        this.dragService.UpdatePosition(this.sessionToken.Value, currentPoint.Point);

        this.LogMove(currentPoint.Point);
    }

    /// <inheritdoc/>
    public bool CompleteDrag()
    {
        if (!this.isActive)
        {
            this.LogDropIgnored();
            return false;
        }

        // this.LogDrop(finalPoint.Point, targetStrip, targetIndex);

        // End visual session first
        if (this.sessionToken.HasValue)
        {
            this.dragService.EndSession(this.sessionToken.Value);
            this.sessionToken = null;
        }

        // Reset state
        this.isActive = false;
        this.context = null;
        this.descriptor = null;

        // Drop outside any TabStrip - this should trigger TearOut event
        return false; // Let coordinator handle TearOut event
    }

    private RenderTargetBitmap? CaptureHeaderImage()
    {
        if (this.context is not { TabStrip: { } strip })
        {
            this.LogHeaderCaptureFailed("no context");
            return null;
        }

        var tabStripItem = strip.GetTabStripItemForItem(this.context.DraggedItem);
        if (tabStripItem is null)
        {
            this.LogHeaderCaptureFailed("no visual container");
            return null;
        }

        try
        {
            // Ensure the visual is loaded and has a valid size
            if (tabStripItem.ActualWidth <= 0 || tabStripItem.ActualHeight <= 0)
            {
                this.LogHeaderCaptureFailed("TabStripItem has invalid dimensions");
                return null;
            }

            // Create a RenderTargetBitmap and kick off the async render without blocking the UI thread.
            // We return the RTB immediately so callers can use it; pixels will be populated shortly.
            var renderTargetBitmap = new RenderTargetBitmap();

            // If we're on a thread with a DispatcherQueue, enqueue the render; otherwise call directly.
            // Do not block here to avoid deadlocks in UI tests.
            if (tabStripItem.DispatcherQueue?.TryEnqueue(() => _ = renderTargetBitmap.RenderAsync(tabStripItem)) != true)
            {
                // Best-effort direct call if enqueuing isn't possible
                _ = renderTargetBitmap.RenderAsync(tabStripItem);
            }

            this.LogHeaderCaptureSuccess();
            return renderTargetBitmap;
        }
        catch (InvalidOperationException ex)
        {
            this.LogHeaderCaptureException(ex);
            return null;
        }
        catch (Exception ex) when (ex is not InvalidOperationException)
        {
            // Catch any other rendering exceptions (COM exceptions, etc.)
            this.LogHeaderCaptureException(ex);
            return null;
        }
    }

    private void RequestPreviewImage()
    {
        if (this.context is null || this.descriptor is null)
        {
            return;
        }

        try
        {
            // Request preview image via coordinator using our local context (always non-null here)
            this.coordinator.RequestPreviewImage(this.descriptor, this.context);

            this.LogPreviewImageRequested();
        }
        catch (InvalidOperationException ex)
        {
            this.LogPreviewImageException(ex);
        }
    }
}
