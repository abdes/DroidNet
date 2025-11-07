// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Implementation of <see cref="IDragVisualService"/> using a frameless WinUI window for the drag
///     overlay. The overlay is non-activating and provides visual feedback during drag operations.
/// </summary>
/// <remarks>
///     This service uses dependency injection to obtain a window factory and spatial mapper factory,
///     enabling proper coordinate transformations and window management without manual Win32 interop.
/// </remarks>
public partial class DragVisualService : IDragVisualService
{
    private readonly ILogger logger;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly IWindowFactory windowFactory;
    private readonly SpatialMapperFactory spatialMapperFactory;
    private readonly Lock syncLock = new();

    private DragSessionToken? activeToken;
    private DragVisualDescriptor? activeDescriptor;
    private DragOverlayWindow? overlayWindow;
    private SpatialPoint<ScreenSpace> windowPositionOffsets;
    private bool windowIsShown;
    private ISpatialMapper? mapper;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DragVisualService"/> class.
    /// </summary>
    /// <param name="hosting">The <see cref="HostingContext"/> providing the UI thread dispatcher.</param>
    /// <param name="windowFactory">The window factory for creating the overlay window.</param>
    /// <param name="spatialMapperFactory">The spatial mapper factory for coordinate transformations.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public DragVisualService(
        HostingContext hosting,
        IWindowFactory windowFactory,
        SpatialMapperFactory spatialMapperFactory,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(hosting);
        ArgumentNullException.ThrowIfNull(windowFactory);
        ArgumentNullException.ThrowIfNull(spatialMapperFactory);

        this.logger = loggerFactory?.CreateLogger<DragVisualService>() ?? NullLogger<DragVisualService>.Instance;
        this.dispatcherQueue = hosting.Dispatcher;
        this.windowFactory = windowFactory;
        this.spatialMapperFactory = spatialMapperFactory;
        this.LogCreated();
    }

    /// <inheritdoc/>
    public DragSessionToken StartSession(DragVisualDescriptor descriptor, SpatialPoint<PhysicalScreenSpace> initialPosition, SpatialPoint<ScreenSpace> hotspotOffsets)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (this.activeToken.HasValue)
            {
                this.LogSessionAlreadyActive();
                throw new InvalidOperationException("A drag visual session is already active.");
            }

            var token = new DragSessionToken { Id = Guid.NewGuid() };
            this.activeToken = token;
            this.activeDescriptor = descriptor;
            this.windowPositionOffsets = hotspotOffsets;

            // Subscribe to descriptor changes
            this.activeDescriptor.PropertyChanged += this.OnDescriptorPropertyChanged;

            // Create the overlay window using the factory
            this.CreateOverlayWindowAsync().GetAwaiter().GetResult();
            this.mapper = this.spatialMapperFactory(this.overlayWindow, null);

            this.UpdatePosition(token, initialPosition);

            this.LogSessionStarted();

            return token;
        }
    }

    /// <inheritdoc/>
    public void UpdatePosition(DragSessionToken token, SpatialPoint<PhysicalScreenSpace> position)
    {
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                this.LogTokenMismatchUpdatePosition(token);
                return;
            }

            if (this.overlayWindow is null)
            {
                this.LogOverlayWindowNotInitialized();
                return;
            }

            // Subtract windowPositionOffsets to get window position
            Debug.Assert(this.mapper is not null, "Spatial mapper should be initialized when overlay window is created.");
            var windowPosition = (this.mapper.ToPhysicalScreen(position) - this.mapper.ToPhysicalScreen(this.windowPositionOffsets)).ToPoint();

            this.LogPositionUpdated(
                position.Point,
                windowPosition);

            // Move the window using the virtual MoveWindow method
            this.overlayWindow.MoveWindow(new Windows.Graphics.PointInt32(
                (int)Math.Round(windowPosition.X),
                (int)Math.Round(windowPosition.Y)));

            if (!this.windowIsShown)
            {
                // Show the window without activating it (no focus steal)
                this.overlayWindow.ShowNoActivate();
                this.windowIsShown = true;
            }
        }
    }

    /// <inheritdoc/>
    public void EndSession(DragSessionToken token)
    {
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                this.LogTokenMismatchEndSession(token);
                return;
            }

            if (this.activeDescriptor is not null)
            {
                this.activeDescriptor.PropertyChanged -= this.OnDescriptorPropertyChanged;
            }

            this.DestroyOverlayWindow();
            this.windowIsShown = false;

            this.LogSessionEnded();

            this.activeDescriptor = null;
            this.activeToken = null;
            this.mapper = null;
            this.windowPositionOffsets = default;
        }
    }

    /// <inheritdoc/>
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token)
    {
        lock (this.syncLock)
        {
            return !this.activeToken.HasValue || this.activeToken.Value != token
                ? null
                : this.activeDescriptor;
        }
    }

    private void AssertUIThread()
    {
        if (!this.dispatcherQueue.HasThreadAccess)
        {
            throw new InvalidOperationException("IDragVisualService methods must be called from the UI thread.");
        }
    }

    private async Task CreateOverlayWindowAsync()
    {
        // Destroy existing window if any
        this.DestroyOverlayWindow();

        // Get initial size from descriptor
        var requestedSize = this.activeDescriptor?.RequestedSize ?? new Size(200, 100);
        if (requestedSize.Width <= 0 || requestedSize.Height <= 0)
        {
            requestedSize = new Size(400, 400);
        }

        // Create the overlay window using the factory
        // Note: We don't register this window with the window manager since it's a transient overlay
        this.overlayWindow = await this.windowFactory.CreateDecoratedWindow<DragOverlayWindow>(WindowCategory.Frameless).ConfigureAwait(false);
        this.overlayWindow.AppWindow.IsShownInSwitchers = false;

        // Get the window's content element for spatial mapper and data binding
        if (this.overlayWindow.Content is not FrameworkElement element)
        {
            throw new InvalidOperationException("Overlay window content is not a FrameworkElement.");
        }

        // Set the descriptor as the DataContext for data binding
        element.DataContext = this.activeDescriptor;

        // Set the window size
        this.overlayWindow.SetSize(requestedSize);

        this.LogLayeredWindowCreated();
    }

    private void DestroyOverlayWindow()
    {
        if (this.overlayWindow is null)
        {
            return;
        }

        this.overlayWindow.Close();
        this.overlayWindow = null;

        this.LogLayeredWindowDestroyed();
    }

    // Queue render update on UI thread
    private void OnDescriptorPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) =>
        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            lock (this.syncLock)
            {
                // Validate descriptor reference inside lock to ensure consistency
                if (sender is not DragVisualDescriptor descriptor || this.activeDescriptor != descriptor)
                {
                    return;
                }

                // Handle size changes (header/preview images are handled automatically via data binding)
                switch (e.PropertyName)
                {
                    case var name when string.Equals(name, nameof(DragVisualDescriptor.HeaderImage), StringComparison.Ordinal)
                        || string.Equals(name, nameof(DragVisualDescriptor.PreviewImage), StringComparison.Ordinal):
                        // Data binding automatically updates the images
                        this.LogDescriptorPropertyChanged(name ?? "Unknown", "updated via data binding");
                        break;
                    case var name when string.Equals(name, nameof(DragVisualDescriptor.RequestedSize), StringComparison.Ordinal):
                        // Size change can be applied directly
                        this.LogDescriptorPropertyChanged(name ?? "Unknown", "resizing window");
                        var size = descriptor.RequestedSize;
                        if (size.Width > 0 && size.Height > 0)
                        {
                            this.overlayWindow?.SetSize(size);
                        }

                        break;
                }
            }
        });
}
