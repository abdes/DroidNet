// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive;
using System.Reactive.Linq;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Windows.Foundation;
using WinUIEx;
using GridLength = Microsoft.UI.Xaml.GridLength;

namespace DroidNet.Aura;

/// <summary>
/// Represents the main shell view of the application, providing the main user interface and handling window-related events.
/// </summary>
/// <remarks>
/// This class is responsible for initializing the main shell view, setting up the custom title bar, and managing the minimum window width.
/// It decorates the window with a custom title bar, an application icon, and provides a collapsible main menu and a flyout menu for settings and theme selection.
/// </remarks>
[ViewModel(typeof(MainShellViewModel))]
public sealed partial class MainShellView : INotifyPropertyChanged
{
    private double minWindowWidth;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainShellView"/> class.
    /// </summary>
    public MainShellView()
    {
        this.InitializeComponent();

        var uiContext = SynchronizationContext.Current
            ?? throw new InvalidOperationException("SynchronizationContext.Current is null; ensure constructor runs on UI thread.");

        // Convert Loaded and SizeChanged events to observables
        var loadedObservable = Observable.FromEventPattern<RoutedEventHandler, RoutedEventArgs>(
            h => this.CustomTitleBar.Loaded += h,
            h => this.CustomTitleBar.Loaded -= h)
            .Select(_ => Unit.Default);

        var sizeChangedObservable = Observable.FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
            h => this.CustomTitleBar.SizeChanged += h,
            h => this.CustomTitleBar.SizeChanged -= h)
            .Select(_ => Unit.Default);

        var primaryCommandsLayoutObservable = Observable.FromEventPattern<EventHandler<object>, object>(
                h => this.PrimaryCommands.LayoutUpdated += h,
                h => this.PrimaryCommands.LayoutUpdated -= h)
            .Select(_ => Unit.Default);

        var secondaryCommandsLayoutObservable = Observable.FromEventPattern<EventHandler<object>, object>(
                h => this.SecondaryCommands.LayoutUpdated += h,
                h => this.SecondaryCommands.LayoutUpdated -= h)
            .Select(_ => Unit.Default);

        // Merge the observables and throttle the events
        var throttledObservable = loadedObservable
            .Merge(sizeChangedObservable)
            .Merge(primaryCommandsLayoutObservable)
            .Merge(secondaryCommandsLayoutObservable)
            .Throttle(TimeSpan.FromMilliseconds(100))
            .ObserveOn(uiContext);

        // Subscribe to the throttled observable to call SetupCustomTitleBar
        _ = throttledObservable.Subscribe(_ =>
        {
            // Skip scheduling when the title bar has already been unloaded (window closing).
            if (!this.IsLoaded)
            {
                return;
            }

            this.SetupCustomTitleBar();
        });
    }

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the minimum width of the window.
    /// </summary>
    public double MinWindowWidth
    {
        get => this.minWindowWidth;
        private set
        {
            if (Math.Abs(this.minWindowWidth - value) < 0.5f)
            {
                return;
            }

            this.minWindowWidth = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.MinWindowWidth)));

            if (this.ViewModel?.Window is WindowEx window && window.MinWidth < this.MinWindowWidth)
            {
                window.MinWidth = this.MinWindowWidth;
            }
        }
    }

    /// <summary>
    /// Converts a <see cref="Rect"/> to a <see cref="Windows.Graphics.RectInt32"/> based on the specified scale.
    /// </summary>
    /// <param name="bounds">The bounds to convert.</param>
    /// <param name="scale">The scale factor to apply.</param>
    /// <returns>A <see cref="Windows.Graphics.RectInt32"/> representing the scaled bounds.</returns>
    private static Windows.Graphics.RectInt32 GetRect(Rect bounds, double scale) => new(
        _X: (int)Math.Round(bounds.X * scale),
        _Y: (int)Math.Round(bounds.Y * scale),
        _Width: (int)Math.Round(bounds.Width * scale),
        _Height: (int)Math.Round(bounds.Height * scale));

    private static void AddPassthroughRegion(
        FrameworkElement element,
        double scaleAdjustment,
        List<Windows.Graphics.RectInt32> regions)
    {
        if (element.Visibility is not Visibility.Visible || element.ActualWidth <= 0)
        {
            return;
        }

        var transform = element.TransformToVisual(visual: null);
        var bounds = transform.TransformBounds(new Rect(0, 0, element.ActualWidth, element.ActualHeight));

        var scaledRect = GetRect(bounds, scaleAdjustment);
        regions.Add(scaledRect);
    }

    /// <summary>
    /// Sets up the custom title bar for the window.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Configures the custom title bar layout with system caption button reservation.
    /// The title bar uses a 5-column Grid layout:
    /// </para>
    /// <code>
    /// <![CDATA[
    /// Title Bar Grid Layout (5 columns)
    /// ═══════════════════════════════════════════════════════════════════════════
    ///
    /// Visual Layout:
    /// ┌────────────────────────────────────────────────────────────────────────┐
    /// │ ┌──────┬──────────────┬──────────────────┬─────────┬─────────────────┐ │
    /// │ │ Icon │ PrimaryCmd   │   Drag Area (*)  │Secondary│SystemReservedRt │ │
    /// │ │ Auto │ Auto         │   Expands        │ Auto    │ Dynamic (Empty) │ │
    /// │ │  0   │      1       │        2         │    3    │        4        │ │
    /// │ └──────┴──────────────┴──────────────────┴─────────┴─────────────────┘ │
    /// │                                                    ▲                   │
    /// │                                                    └─ System buttons   │
    /// │                                                       drawn HERE by OS │
    /// └────────────────────────────────────────────────────────────────────────┘
    ///
    /// Column Details:
    /// - Column 0: IconColumn (Width="Auto") - Window icon
    /// - Column 1: PrimaryCommandsColumn (Width="Auto") - MenuBar/ExpandableMenuBar
    /// - Column 2: DragColumn (Width="*", MinWidth="48") - Draggable area
    /// - Column 3: SecondaryCommandsColumn (Width="Auto") - Settings menu button
    /// - Column 4: SystemReservedRight (Width=Dynamic) - Empty space for system buttons
    ///
    /// The SystemReservedRight column width is set to appWindow.TitleBar.RightInset
    /// to reserve space where Windows draws the minimize, maximize, and close buttons.
    /// UpdateLayout() is called after setting this width to force Grid recalculation.
    /// ]]>
    /// </code>
    /// </remarks>
    private void SetupCustomTitleBar()
    {
        if (!this.IsLoaded)
        {
            return;
        }

        Debug.Assert(
            this.ViewModel?.Window is not null,
            "expecting a properly setup ViewModel when loaded");

        if (this.ViewModel?.Window is null)
        {
            return;
        }

        if (this.CustomTitleBar.XamlRoot is null)
        {
            return;
        }

        var appWindow = this.ViewModel.Window.AppWindow;
        var scaleAdjustment = this.CustomTitleBar.XamlRoot.RasterizationScale;

        // Note: Height is now controlled by XAML binding to Context.Decorations.TitleBar.Height
        // Configure system insets for caption buttons
        // The system insets tell us how much space Windows needs for the caption buttons.
        // We must reserve this full space with empty padding columns.
        var rightInset = appWindow.TitleBar.RightInset / scaleAdjustment;

        this.SystemReservedRight.Width = new GridLength(rightInset);

        // Force Grid to recalculate layout with the new SystemReservedRight width
        this.CustomTitleBar.UpdateLayout();

        // Configure passthrough regions for interactive elements
        // Pass the system right inset in device pixels so we can clamp passthrough regions
        this.ConfigurePassthroughRegions(appWindow.Id, scaleAdjustment, appWindow.TitleBar.RightInset);

        // Calculate minimum window width
        this.MinWindowWidth = this.IconColumn.ActualWidth +
                              this.PrimaryCommands.ActualWidth + this.DragColumn.MinWidth +
                              this.SecondaryCommands.ActualWidth +
                              this.SystemReservedRight.Width.Value;
    }

    /// <summary>
    /// Configures passthrough regions for interactive elements in the title bar.
    /// </summary>
    /// <param name="windowId">The window ID.</param>
    /// <param name="scaleAdjustment">The DPI scale adjustment factor.</param>
    /// <param name="systemRightInsetDevice">The system right inset in device pixels.</param>
    private void ConfigurePassthroughRegions(Microsoft.UI.WindowId windowId, double scaleAdjustment, int systemRightInsetDevice)
    {
        // NOTE: Passthrough regions must be calculated relative to the window.
        // When we use TransformToVisual(null), the element's position already includes
        // the CustomTitleBar margin, so we don't need to add it separately.
        var passthroughRegions = new List<Windows.Graphics.RectInt32>();

        AddPassthroughRegion(this.PrimaryCommands, scaleAdjustment, passthroughRegions);
        AddPassthroughRegion(this.SecondaryCommands, scaleAdjustment, passthroughRegions);

        // Clamp regions so they never touch or overlap the system caption area on the right.
        // Work in device pixels.
        var clampedRegions = new List<Windows.Graphics.RectInt32>();

        // Compute device window width from logical width and rasterization scale
        var deviceWindowWidth = (int)Math.Round(this.ActualWidth * scaleAdjustment);

        // Leave a small gap between passthrough regions and system area.
        var gap = Math.Max(2, (int)this.SecondaryCommands.Margin.Right);
        var allowedMaxX = deviceWindowWidth - systemRightInsetDevice - gap;

        foreach (var r in passthroughRegions)
        {
            // If region starts beyond allowed area, skip
            if (r.X >= allowedMaxX)
            {
                continue;
            }

            var right = r.X + r.Width;
            var clampedWidth = r.Width;
            if (right > allowedMaxX)
            {
                clampedWidth = Math.Max(0, allowedMaxX - r.X);
            }

            if (clampedWidth > 0)
            {
                clampedRegions.Add(new Windows.Graphics.RectInt32(_X: r.X, _Y: r.Y, _Width: clampedWidth, _Height: r.Height));
            }
        }

        // Set passthrough regions (use empty array to clear if none)
        var nonClientInputSrc = InputNonClientPointerSource.GetForWindowId(windowId);
        nonClientInputSrc.SetRegionRects(NonClientRegionKind.Passthrough, [.. clampedRegions]);
    }
}
