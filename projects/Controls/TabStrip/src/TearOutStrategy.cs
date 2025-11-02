// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
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
    public void InitiateDrag(DragContext context, SpatialPoint initialPoint)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (this.isActive)
        {
            this.LogAlreadyActive();
            throw new InvalidOperationException("TearOutStrategy is already active");
        }

        this.context = context;
        this.isActive = true;

        // Convert to Screen coordinates (TearOutStrategy works in screen space for the overlay)
        var screenPoint = initialPoint.To(CoordinateSpace.Screen, context.SourceStrip);

        this.LogEnterTearOutMode(screenPoint.Point);

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
        this.sessionToken = this.dragService.StartSession(this.descriptor, context.Hotspot.Point);

        // Update initial position
        this.dragService.UpdatePosition(this.sessionToken.Value, screenPoint.Point);
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint currentPoint)
    {
        if (!this.isActive || !this.sessionToken.HasValue)
        {
            this.LogMoveIgnored();
            return;
        }

        // Convert to Screen coordinates
        var screenPoint = currentPoint.To(CoordinateSpace.Screen, this.context!.SourceStrip);

        // Update overlay position
        this.dragService.UpdatePosition(this.sessionToken.Value, screenPoint.Point);

        this.LogMove(screenPoint.Point);
    }

    /// <inheritdoc/>
    public bool CompleteDrag(SpatialPoint finalPoint, TabStrip? targetStrip, int? targetIndex)
    {
        if (!this.isActive)
        {
            this.LogDropIgnored();
            return false;
        }

        // Convert to Screen coordinates for logging
        var screenPoint = finalPoint.To(CoordinateSpace.Screen, this.context!.SourceStrip);

        this.LogDrop(screenPoint.Point, targetStrip, targetIndex);

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

        // If dropping over a TabStrip, it should handle the insertion
        if (targetStrip is not null && targetIndex.HasValue)
        {
            this.LogDropOnTabStrip(targetStrip, targetIndex.Value);
            return true;
        }

        // Drop outside any TabStrip - this should trigger TearOut event
        this.LogDropOutside(screenPoint.Point);
        return false; // Let coordinator handle TearOut event
    }

    private RenderTargetBitmap? CaptureHeaderImage()
    {
        if (this.context?.SourceVisualItem is null)
        {
            this.LogHeaderCaptureFailed("no source visual item");
            return null;
        }

        try
        {
            var tabStripItem = this.context.SourceVisualItem;

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
