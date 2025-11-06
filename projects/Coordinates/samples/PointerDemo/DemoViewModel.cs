// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura.Windowing;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Coordinates.PointerDemo;

/// <summary>
///     View model that maps pointer coordinates across screen, window, and element spaces.
/// </summary>
/// <remarks>
///     Initializes a new instance of the <see cref="DemoViewModel" /> class.
/// </remarks>
/// <param name="spatialMapperFactory">Factory used to create spatial mappers for UI elements.</param>
/// <param name="windowManagerService">Service publishing Aura window lifecycle events.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class DemoViewModel(SpatialMapperFactory spatialMapperFactory, IWindowManagerService windowManagerService) : ObservableObject
{
    private const string PixelFormat = "F0";
    private const string DipFormat = "F2";
    private static readonly string ZeroPhysical = FormatPoint(new Point(0, 0), PixelFormat);
    private static readonly string ZeroLogical = FormatPoint(new Point(0, 0), DipFormat);

    private readonly SpatialMapperFactory spatialMapperFactory = spatialMapperFactory ?? throw new ArgumentNullException(nameof(spatialMapperFactory));
    private readonly IWindowManagerService windowManagerService = windowManagerService ?? throw new ArgumentNullException(nameof(windowManagerService));

    private IDisposable? windowEventsSubscription;
    private FrameworkElement? trackedElement;

    private DispatcherQueueTimer? updateTimer;
    private ISpatialMapper? mapper;
    private Point lastElementPoint;
    private Window? trackedWindow;

    /// <summary>Gets the physical screen coordinates formatted for display.</summary>
    [ObservableProperty]
    public partial string PhysicalScreen { get; set; } = ZeroPhysical;

    /// <summary>Gets the logical screen coordinates formatted for display.</summary>
    [ObservableProperty]
    public partial string LogicalScreen { get; set; } = ZeroLogical;

    /// <summary>Gets the window client coordinates formatted for display.</summary>
    [ObservableProperty]
    public partial string WindowClient { get; set; } = ZeroLogical;

    /// <summary>Gets the element-relative coordinates formatted for display.</summary>
    [ObservableProperty]
    public partial string ElementBox { get; set; } = ZeroLogical;

    /// <summary>Gets the width assigned to the demonstration surface.</summary>
    [ObservableProperty]
    public partial double SurfaceWidth { get; set; }

    /// <summary>Gets the height assigned to the demonstration surface.</summary>
    [ObservableProperty]
    public partial double SurfaceHeight { get; set; }

    /// <summary>Gets the monitor information for the current cursor location.</summary>
    [ObservableProperty]
    public partial WindowMonitorInfo MonitorInfo { get; set; }

    /// <summary>Gets the DPI and window/element information for the tracked window.</summary>
    [ObservableProperty]
    public partial WindowInfo WindowInfo { get; set; }

    /// <summary>Gets the element's logical coordinates (top-left) relative to the window for display.</summary>
    [ObservableProperty]
    public partial string ElementRelativeWindow { get; set; } = ZeroLogical;

    /// <summary>Gets the DPI (as string) of the monitor currently under the pointer.</summary>
    [ObservableProperty]
    public partial string PointerMonitorDpi { get; set; } = "96";

    /// <summary>
    ///     Starts tracking the pointer relative to the provided element and its owning window.
    /// </summary>
    /// <param name="element">The element used as the element-space origin.</param>
    public void StartTracking(FrameworkElement element)
    {
        ArgumentNullException.ThrowIfNull(element);

        this.trackedElement = element;

        // Subscribe to element lifecycle and sizing so we always use the exact
        // measured element size and position (no heuristics).
        this.trackedElement.Loaded += this.OnTrackedElementLoaded;
        this.trackedElement.SizeChanged += this.OnTrackedElementSizeChanged;

        this.EnsureWindowEventsSubscription();

        var activeContext = this.windowManagerService.ActiveWindow;
        if (activeContext is not null)
        {
            this.HandleWindowActivation(activeContext);
        }
    }

    /// <summary>
    ///     Stops tracking the pointer and releases associated resources.
    /// </summary>
    public void StopTracking()
    {
        this.ResetTrackingTimer();

        this.windowEventsSubscription?.Dispose();
        this.windowEventsSubscription = null;
        if (this.trackedElement is not null)
        {
            this.trackedElement.Loaded -= this.OnTrackedElementLoaded;
            this.trackedElement.SizeChanged -= this.OnTrackedElementSizeChanged;
        }

        this.trackedElement = null;
        this.trackedWindow = null;
    }

    private static string FormatPoint(Point point, string format)
    {
        var culture = CultureInfo.InvariantCulture;
        var x = point.X.ToString(format, culture);
        var y = point.Y.ToString(format, culture);
        return $"({x}, {y})";
    }

    private void UpdateSurfaceFromElement()
    {
        if (this.trackedElement is null)
        {
            return;
        }

        // Use the exact measured size of the element. If the element hasn't been
        // measured yet, do not attempt heuristics.
        if (this.trackedElement.ActualWidth <= 0 || this.trackedElement.ActualHeight <= 0)
        {
            return;
        }

        var width = this.trackedElement.ActualWidth;
        var height = this.trackedElement.ActualHeight;

        this.SurfaceWidth = width;
        this.SurfaceHeight = height;
        this.RefreshBoxLogicalWindow();
    }

    private void EnsureWindowEventsSubscription()
    {
        if (this.windowEventsSubscription is not null)
        {
            return;
        }

        this.windowEventsSubscription = this.windowManagerService.WindowEvents.Subscribe(this.OnWindowLifecycleEvent);
    }

    private void HandleWindowActivation(WindowContext context)
    {
        if (this.trackedElement is null)
        {
            return;
        }

        var window = context.Window;

        if (!this.IsElementHostedByWindow(window))
        {
            return;
        }

        if (ReferenceEquals(this.trackedWindow, window) && this.mapper is not null)
        {
            return;
        }

        this.BeginTracking(window, this.trackedElement);
    }

    private bool IsElementHostedByWindow(Window window)
        => this.trackedElement?.XamlRoot is not null && window.Content?.XamlRoot == this.trackedElement.XamlRoot;

    private void OnWindowLifecycleEvent(WindowLifecycleEvent lifecycleEvent)
    {
        switch (lifecycleEvent.EventType)
        {
            case WindowLifecycleEventType.Activated:
                this.HandleWindowActivation(lifecycleEvent.Context);
                break;
            case WindowLifecycleEventType.Closed when ReferenceEquals(this.trackedWindow, lifecycleEvent.Context.Window):
                this.ResetTrackingTimer();
                this.trackedWindow = null;
                break;
        }
    }

    private void BeginTracking(Window window, FrameworkElement element)
    {
        this.ResetTrackingTimer();

        this.mapper = this.spatialMapperFactory(window, element);
        this.trackedWindow = window;

        var dispatcher = window.DispatcherQueue
            ?? DispatcherQueue.GetForCurrentThread()
            ?? throw new InvalidOperationException("UI dispatcher queue is not available.");

        this.updateTimer = dispatcher.CreateTimer();
        this.updateTimer.Interval = TimeSpan.FromMilliseconds(33);
        this.updateTimer.IsRepeating = true;
        this.updateTimer.Tick += this.OnUpdateTick;
        this.updateTimer.Start();
    }

    private void ResetTrackingTimer()
    {
        if (this.updateTimer is not null)
        {
            this.updateTimer.Stop();
            this.updateTimer.Tick -= this.OnUpdateTick;
            this.updateTimer = null;
        }

        this.mapper = null;
    }

    private void OnUpdateTick(object? sender, object? e)
    {
        _ = sender;
        _ = e;

        // Always get cursor first so we can show pointer-related info (DPI under pointer)
        if (!Native.GetCursorPos(out var cursor))
        {
            return;
        }

        var physicalPoint = new Point(cursor.X, cursor.Y);

        // Update the DPI for the monitor currently under the pointer
        var pdpi = Coordinates.Native.GetDpiForPhysicalPoint(physicalPoint);
        this.PointerMonitorDpi = pdpi.ToString(CultureInfo.InvariantCulture);

        this.UpdateCoordinates(physicalPoint);
    }

    private void UpdateCoordinates(Point physicalPoint)
    {
        var physicalSpatial = new SpatialPoint<PhysicalScreenSpace>(physicalPoint);

        if (this.mapper is null)
        {
            return;
        }

        var logicalScreenPoint = this.mapper.ToScreen(physicalSpatial).Point;
        var windowPoint = this.mapper.ToWindow(physicalSpatial).Point;
        var elementPoint = this.mapper.ToElement(physicalSpatial).Point;

        this.lastElementPoint = elementPoint;

        this.PhysicalScreen = FormatPoint(physicalPoint, PixelFormat);
        this.LogicalScreen = FormatPoint(logicalScreenPoint, DipFormat);
        this.WindowClient = FormatPoint(windowPoint, DipFormat);
        this.ElementBox = FormatPoint(elementPoint, DipFormat);

        // Update monitor / window information in a small helper for clarity
        this.UpdateWindowMonitorInfo();
    }

    private void RefreshBoxLogicalWindow()
    {
        // Exact calculation: return the element's absolute top-left within the window
        // using the visual transform to the root (null). No fallbacks — fail fast
        // if the visual transform is not available.
        if (this.trackedElement is null)
        {
            // clear element position display
            this.ElementRelativeWindow = ZeroLogical;
            return;
        }

        var transform = this.trackedElement.TransformToVisual(visual: null);
        var topLeft = transform.TransformPoint(new Point(0, 0));
        this.ElementRelativeWindow = FormatPoint(topLeft, DipFormat);
    }

    private void OnTrackedElementLoaded(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.UpdateSurfaceFromElement();
        this.RefreshBoxLogicalWindow();
    }

    private void OnTrackedElementSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.UpdateSurfaceFromElement();
        this.RefreshBoxLogicalWindow();
    }

    private void UpdateWindowMonitorInfo()
    {
        if (this.trackedWindow is null)
        {
            return;
        }

        // Prefer using the mapper for window/monitor information when available
        if (this.mapper is not null)
        {
            var winInfo = this.mapper.WindowInfo;

            // Assign the shared WindowInfo struct directly (display helpers available on the struct)
            this.WindowInfo = winInfo;

            // Update element-relative display now that window info is present
            this.RefreshBoxLogicalWindow();

            var monInfo = this.mapper.WindowMonitorInfo;
            if (monInfo.MonitorHandle != IntPtr.Zero)
            {
                this.MonitorInfo = monInfo;
            }
        }
    }

    private static partial class Native
    {
        private const string User32 = "user32.dll";

        [LibraryImport(User32, SetLastError = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static partial bool GetCursorPos(out POINT lpPoint);

        /// <summary>POINT structure for screen coordinates.</summary>
        /// <remarks>
        /// Initializes a new instance of the <see cref="POINT"/> struct.
        /// </remarks>
        /// <param name="x">The x-coordinate.</param>
        /// <param name="y">The y-coordinate.</param>
        [StructLayout(LayoutKind.Sequential)]
        public struct POINT(int x, int y)
        {
            /// <summary>
            /// The x-coordinate of the point.
            /// </summary>
            public int X = x;

            /// <summary>
            /// The y-coordinate of the point.
            /// </summary>
            public int Y = y;
        }
    }
}
