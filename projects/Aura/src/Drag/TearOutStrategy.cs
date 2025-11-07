// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Media.Imaging;

namespace DroidNet.Aura.Drag;

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
    public void InitiateDrag(DragContext context, SpatialPoint<PhysicalScreenSpace> position)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (this.isActive)
        {
            this.LogAlreadyActive();
            throw new InvalidOperationException("TearOutStrategy is already active");
        }

        this.context = context;
        this.isActive = true;

        this.LogEnterTearOutMode(position.Point);

        // Capture header image from the source visual item
        var headerImage = this.CaptureHeaderImage();

        // Create descriptor for the drag visual
        this.descriptor = new DragVisualDescriptor
        {
            HeaderImage = headerImage,
            RequestedSize = new Windows.Foundation.Size(400, 200), // Default size
        };

        // Request preview image from the application
        this.RequestPreviewImage(this.descriptor);

        // Start the drag visual session with the window position offsets calculated from the initial drag point
        this.sessionToken = this.dragService.StartSession(
            this.descriptor,
            position,
            this.context.HotspotOffsets.AsScreen());
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint<PhysicalScreenSpace> position)
    {
        if (!this.isActive || !this.sessionToken.HasValue)
        {
            this.LogMoveIgnored();
            return;
        }

        // Update overlay position
        this.dragService.UpdatePosition(this.sessionToken.Value, position);

        this.LogMove(position.Point);
    }

    /// <inheritdoc/>
    public int? CompleteDrag(bool drop)
    {
        void ResetState()
        {
            this.isActive = false;
            this.context = null;
            this.descriptor = null;
        }

        if (!drop)
        {
            this.LogDragCompletedNoDrop();

            // With no drop, completing the drag simply resets state
            ResetState();
        }

        if (!this.isActive)
        {
            this.LogDropIgnored();
            return null;
        }

        // this.LogDrop(finalPoint.Point, targetStrip, targetIndex);

        // End visual session first
        if (this.sessionToken.HasValue)
        {
            this.dragService.EndSession(this.sessionToken.Value);
            this.sessionToken = null;
        }

        ResetState();

        // Drop outside any TabStrip - this should trigger TearOut event
        // Return null since the item isn't in any TabStrip yet (application will handle window creation)
        return null;
    }

    private RenderTargetBitmap? CaptureHeaderImage()
    {
        if (this.context is not { TabStrip: { } strip })
        {
            this.LogHeaderCaptureFailed("no context");
            return null;
        }

        if (this.context.DraggedVisualElement is not { } tabStripItem)
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

    /// <summary>
    /// Requests a preview image from the source TabStrip for the dragged item.
    /// This is called by TearOutStrategy when transitioning to TearOut mode.
    /// The source TabStrip will raise its TabDragImageRequest event, allowing the application to provide a preview.
    /// </summary>
    /// <param name="descriptor">The drag visual descriptor that can be updated with a preview image.</param>
    private void RequestPreviewImage(DragVisualDescriptor descriptor)
    {
        if (this.context is null)
        {
            return;
        }

        try
        {
            // Ask the source TabStrip to raise its TabDragImageRequest event
            // This allows the application to handle the event and provide a custom preview
            this.context.TabStrip?.RequestPreviewImage(this.context.DraggedItem, descriptor);
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            // Log the exception but don't let it crash the drag operation
            this.LogPreviewImageException(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }
}
