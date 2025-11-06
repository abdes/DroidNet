// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

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
    private ISpatialMapper? spatialMapper;
    private SpatialPoint<ScreenSpace> hotspot;

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
    public DragSessionToken StartSession(DragVisualDescriptor descriptor, Windows.Foundation.Point logicalHotspot)
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
            this.hotspot = logicalHotspot.AsScreen();

            // Subscribe to descriptor changes
            this.activeDescriptor.PropertyChanged += this.OnDescriptorPropertyChanged;

            // Create the overlay window using the factory
            this.CreateOverlayWindowAsync().GetAwaiter().GetResult();

            // Create the spatial mapper with the window for Screen↔Physical conversions
            // Element is not needed since we only do screen-space conversions
            this.spatialMapper = this.spatialMapperFactory(window: this.overlayWindow, element: null);

            this.LogSessionStarted();

            return token;
        }
    }

    /// <inheritdoc/>
    public void UpdatePosition(DragSessionToken token, SpatialPoint<PhysicalScreenSpace> physicalScreenPoint)
    {
        this.AssertUIThread();

        lock (this.syncLock)
        {
            if (!this.activeToken.HasValue || this.activeToken.Value != token)
            {
                this.LogTokenMismatchUpdatePosition(token);
                return;
            }

            if (this.overlayWindow is null || this.spatialMapper is null)
            {
                this.LogOverlayWindowNotInitialized();
                return;
            }

            // Convert physical screen point to logical screen space
            var logicalCursor = this.spatialMapper.ToScreen(physicalScreenPoint);

            // Subtract hotspot to get window position
            var logicalWindowPosition = new Windows.Foundation.Point(
                logicalCursor.Point.X - this.hotspot.Point.X,
                logicalCursor.Point.Y - this.hotspot.Point.Y);

            // Convert back to physical for AppWindow positioning
            var physicalWindowPosition = this.spatialMapper.ToPhysicalScreen(logicalWindowPosition.AsScreen());

            this.LogPositionUpdated(
                physicalScreenPoint.Point,
                this.spatialMapper.WindowInfo.WindowDpi,
                physicalWindowPosition.Point);

            // Move the window using the virtual MoveWindow method
            this.overlayWindow.MoveWindow(new Windows.Graphics.PointInt32(
                (int)Math.Round(physicalWindowPosition.Point.X),
                (int)Math.Round(physicalWindowPosition.Point.Y)));
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

            this.LogSessionEnded();

            this.activeDescriptor = null;
            this.activeToken = null;
            this.hotspot = default;
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
        var requestedSize = this.activeDescriptor?.RequestedSize ?? new Windows.Foundation.Size(200, 100);
        if (requestedSize.Width <= 0 || requestedSize.Height <= 0)
        {
            requestedSize = new Windows.Foundation.Size(200, 100);
        }

        // Create the overlay window using the factory
        // Note: We don't register this window with the window manager since it's a transient overlay
        this.overlayWindow = await this.windowFactory.CreateWindow<DragOverlayWindow>().ConfigureAwait(false);

        // Get the window's content element for spatial mapper and data binding
        if (this.overlayWindow.Content is not FrameworkElement element)
        {
            throw new InvalidOperationException("Overlay window content is not a FrameworkElement.");
        }

        // Set the descriptor as the DataContext for data binding
        element.DataContext = this.activeDescriptor;

        // Set the window size
        this.overlayWindow.SetSize(requestedSize);

        // Show the window without activating it (no focus steal)
        this.overlayWindow.ShowNoActivate();

        this.LogLayeredWindowCreated();
    }

    private void DestroyOverlayWindow()
    {
        this.overlayWindow?.Close();
        this.overlayWindow = null;

        this.spatialMapper = null;

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
