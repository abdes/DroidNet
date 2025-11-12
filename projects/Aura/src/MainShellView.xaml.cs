// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using DroidNet.Aura.Documents;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Input;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Foundation;
using Windows.Graphics;
using WinUIEx;
using GridLength = Microsoft.UI.Xaml.GridLength;

namespace DroidNet.Aura;

/// <summary>
///     Represents the main shell view of the application, providing the main user interface and
///     handling window-related events.
/// </summary>
/// <remarks>
///     This class is responsible for initializing the main shell view, setting up the custom title
///     bar, and managing the minimum window width. It decorates the window with a custom title bar,
///     an icon, an optional main menu and a flyout menu for settings and theme selection.
/// </remarks>
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "The view owns the DocumentTabPresenter and disposes it in the Unloaded handler to match WinUI control lifetime semantics.")]
[ViewModel(typeof(MainShellViewModel))]
public sealed partial class MainShellView : INotifyPropertyChanged
{
    // Epsilon (in device-independent pixels) used when comparing GridLength widths
    private const double WidthEpsilon = 0.5;

    private ILogger logger = NullLogger<MainShellView>.Instance;
    private double minWindowWidth;

    // Holds the subscription for titlebar/layout observables so we can dispose it on Unloaded
    private IDisposable? titlebarSubscription;
    private IDocumentService? cachedDocumentService;
    private DocumentTabPresenter? documentTabPresenter;

    // Last applied passthrough regions (device pixels). Used to avoid re-applying identical regions.
    private RectInt32[]? lastAppliedPassthroughRegions;
    private bool passthroughUnavailable;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainShellView"/> class.
    /// </summary>
    public MainShellView()
    {
        this.InitializeComponent();

        var uiContext = SynchronizationContext.Current
            ?? throw new InvalidOperationException("SynchronizationContext.Current is null; ensure constructor runs on UI thread.");

        this.Loaded += (_, _) =>
        {
            this.logger = this.ViewModel?.LoggerFactory?.CreateLogger<MainShellView>() ?? NullLogger<MainShellView>.Instance;
            this.LogLoaded();

            // Adjust height of the title bar row
            var singleRowHeight = this.ViewModel?.Context?.Decorations?.TitleBar?.Height ?? 32.0;
            var withDocumentTabs = this.ViewModel?.Context?.Decorations?.TitleBar?.WithDocumentTabs == true;
            this.TitleBarRow.Height = new GridLength(singleRowHeight);
            this.DocumentTabsRow.Height = withDocumentTabs ? new GridLength(singleRowHeight) : new GridLength(0);
            this.AppIcon.Margin = withDocumentTabs ? new Thickness(8, 8, 12, 8) : new Thickness(4, 4, 8, 4);

            // ViewModel is now properly set up; Update the logger if the ViewModel changes for consistency
            this.ViewModelChanged -= this.InitializeLogger; // Should not be needed, but ensure no duplicates
            this.ViewModelChanged += this.InitializeLogger;

            this.ObserveTitleBar(uiContext);
            this.AttachDocumentServiceHandlers();
        };

        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.InitializeLogger;

            this.ForgetTitleBar();
            this.DetachDocumentServiceHandlers();

            this.LogUnloaded();
        };
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

            // Ensure the host window enforces the computed minimum width.
            // Assign unconditionally to avoid cases where the window's MinWidth was never
            // updated (or updated with an incompatible unit) and the user can resize below
            // the intended minimum. The operation is cheap and idempotent from the app's
            // perspective; the platform will clamp it if necessary.
            if (this.ViewModel?.Window is WindowEx wnd) // TODO: eliminate the need for WindowEx
            {
                // Use a ceiling to avoid fractional DIP rounding issues that the host
                // windowing subsystem might ignore; enforce an integer DIP minimum.
                wnd.MinWidth = Math.Ceiling(this.MinWindowWidth);
            }
        }
    }

    private void InitializeLogger(object? sender, ViewModelChangedEventArgs<MainShellViewModel> args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.logger = this.ViewModel?.LoggerFactory.CreateLogger<MainShellView>() ?? NullLogger<MainShellView>.Instance;

        // Log that the instance logger was replaced (this helps track log sinks/lifetimes)
        this.LogLoggerInitialized();
    }

    private void ObserveTitleBar(SynchronizationContext uiContext)
    {
        IObservable<string> ObserveWindowSize()
        {
            var window = this.ViewModel?.Window;
            Debug.Assert(window is not null, "expecting a properly setup ViewModel when loaded");
            return Observable
                .FromEventPattern<TypedEventHandler<object, WindowSizeChangedEventArgs>, WindowSizeChangedEventArgs>(
                h => h.Invoke,
                h => window.SizeChanged += h,
                h => window.SizeChanged -= h)
                .Select(_ => "Window.SizeChanged");
        }

        IObservable<string> ObserveTitleBarLoaded() => Observable
            .FromEventPattern<RoutedEventHandler, RoutedEventArgs>(
                h => this.CustomTitleBar.Loaded += h,
                h => this.CustomTitleBar.Loaded -= h)
            .Select(_ => "CustomTitleBar.Loaded");

        IObservable<string> ObserveTitleBarSize() => Observable
            .FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                h => this.CustomTitleBar.SizeChanged += h,
                h => this.CustomTitleBar.SizeChanged -= h)
            .Select(_ => "CustomTitleBar.SizeChanged");

        IObservable<string> ObservePrimaryCommandsSize() => Observable
            .FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                h => this.PrimaryCommands.SizeChanged += h,
                h => this.PrimaryCommands.SizeChanged -= h)
            .Select(_ => "PrimaryCommands.SizeChanged");

        IObservable<string> ObserveSecondaryCommandsSize() => Observable
            .FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                h => this.SecondaryCommands.SizeChanged += h,
                h => this.SecondaryCommands.SizeChanged -= h)
            .Select(_ => "SecondaryCommands.SizeChanged");

        // Merge the observables and throttle the events
        // Compute a lightweight key representing the current passthrough geometry and only
        // emit when that key changes. This short-circuits layout noise that doesn't affect
        // the regions we apply for non-client input.
        var throttledObservable =
            ObserveWindowSize()
            .Merge(ObserveTitleBarLoaded())
            .Merge(ObserveTitleBarSize())
            .Merge(ObservePrimaryCommandsSize())
            .Merge(ObserveSecondaryCommandsSize())
            .Throttle(TimeSpan.FromMilliseconds(10))
            .ObserveOn(uiContext)
            .Select(evt => evt);

        // Subscribe to the throttled observable to call SetupCustomTitleBar
        this.LogObservablesSubscribed();

        // Dispose any previous subscription before creating a new one (defensive)
        this.titlebarSubscription?.Dispose();
        this.titlebarSubscription = throttledObservable.Subscribe(eventType =>
        {
            this.LogThrottledTitlebarEvent(eventType);

            if (!this.IsLoaded)
            {
                return; // Skip scheduling when the title bar has already been unloaded (window closing).
            }

            this.SetupCustomTitleBar();
            this.SetWindowMinWidth();
            this.UpdateSecondaryCommandsVisibilityWithHysteresis();
        });
    }

    private void AttachDocumentServiceHandlers()
    {
        var ds = this.ViewModel?.DocumentService;
        if (ReferenceEquals(ds, this.cachedDocumentService))
        {
            return; // nothing to do
        }

        // Detach from previous
        this.DetachDocumentServiceHandlers();

        if (ds is null)
        {
            this.cachedDocumentService = null;
            return;
        }

        this.cachedDocumentService = ds;
        if (this.ViewModel?.Context is not null)
        {
            this.documentTabPresenter = new DocumentTabPresenter(this.DocumentTabStrip, ds, this.ViewModel.Context, this.DispatcherQueue, this.logger);
        }
    }

    private void DetachDocumentServiceHandlers()
    {
        if (this.cachedDocumentService is not null)
        {
            // Presenter owns event subscriptions; nothing to unsubscribe here.
            this.cachedDocumentService = null;
        }

        // Dispose the presenter which will unsubscribe from events and clear tabs
        this.documentTabPresenter?.Dispose();
        this.documentTabPresenter = null;
    }

    private void ForgetTitleBar()
    {
        this.titlebarSubscription?.Dispose();
        this.titlebarSubscription = null;
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
    /// <para><b>Note:</b></para>
    /// <para>
    /// The documents <c>TabStrip</c> is not part of the drag area in the title
    /// bar, and as such is excluded from the observed layout and from the
    /// passthrough region calculations.
    /// </para>
    /// </remarks>
    private void SetupCustomTitleBar()
    {
        Debug.Assert(this.IsLoaded, "SetupCustomTitleBar should only be called when the view is loaded.");
        Debug.Assert(this.ViewModel?.Window is not null, "expecting a properly setup ViewModel when loaded");

        this.LogSetupCustomTitleBarInvoked();

        var appWindow = this.ViewModel.Window.AppWindow;
        var scaleAdjustment = this.CustomTitleBar.XamlRoot.RasterizationScale;

        UpdateSystemReservedWidthIfChanged();

        // Configure passthrough regions for interactive elements
        // Pass the system right inset in device pixels so we can clamp passthrough regions
        this.ConfigurePassthroughRegions(appWindow, scaleAdjustment);

        void UpdateSystemReservedWidthIfChanged()
        {
            // The system insets tell us how much space Windows needs for the caption buttons.
            // We must reserve this full space with empty padding columns.
            var rightInset = appWindow.TitleBar.RightInset / scaleAdjustment;
            var previousReserved = this.SystemReservedRight.Width.Value;

            // Only update the reserved width when the numeric value meaningfully changed. Re-assigning
            // the same GridLength can provoke layout; guard with an epsilon compare to reduce churn.
            if (double.IsNaN(previousReserved) || Math.Abs(previousReserved - rightInset) > WidthEpsilon)
            {
                this.SystemReservedRight.Width = new GridLength(rightInset);
                this.CustomTitleBar.UpdateLayout(); // Force layout recalculation
                this.LogCustomTitleBarLayout(scaleAdjustment, rightInset);
            }
            else
            {
                this.LogSystemReservedWidthUnchanged(previousReserved, rightInset);
            }
        }
    }

    private void SetWindowMinWidth()
    {
        this.MinWindowWidth = this.IconColumn.ActualWidth +
                      this.PrimaryCommands.ActualWidth + this.DragColumn.MinWidth +
                      this.SecondaryCommands.ActualWidth +
                      this.SystemReservedRight.Width.Value;
        this.LogSetWindowMinWidth();
    }

    /// <summary>
    /// Update the visibility of the SecondaryCommands panel with hysteresis around
    /// the computed MinWindowWidth threshold. This breaks the circular dependency
    /// between the VisualState system and MinWindowWidth that caused layout churn.
    /// </summary>
    private void UpdateSecondaryCommandsVisibilityWithHysteresis()
    {
        var scaleAdjustment = this.CustomTitleBar?.XamlRoot?.RasterizationScale ?? 1.0;
        var deviceWindowWidth = this.ViewModel?.Window?.AppWindow.Size.Width ?? 0;
        var windowWidth = deviceWindowWidth / scaleAdjustment;
        if (double.IsNaN(windowWidth) || windowWidth <= 0.0)
        {
            return;
        }

        // Threshold that must be satisfied before secondary can be shown
        var thresholdWithoutSecondary =
            this.IconColumn.ActualWidth +
            this.PrimaryCommands.ActualWidth +
            this.DragColumn.MinWidth +
            this.SystemReservedRight.Width.Value;

        // Latest measurement and remembered width (for when collapsed)
        var secondaryWidth = this.SecondaryCommands.ActualWidth;

        var currentlyVisible = this.SecondaryCommands.Visibility is Visibility.Visible;
        var requiredWidthToShowSecondary = thresholdWithoutSecondary + secondaryWidth;

        var targetVisible = secondaryWidth > 0.0 && windowWidth >= requiredWidthToShowSecondary;

        // Apply decision once
        var newVisibility = targetVisible ? Visibility.Visible : Visibility.Collapsed;

        // Apply only if it changed
        if (this.SecondaryCommands.Visibility != newVisibility)
        {
            this.SecondaryCommands.Visibility = newVisibility;
        }

        // Single log statement with all relevant info
        this.LogSecondaryCommandsInfo(newVisibility, currentlyVisible, windowWidth, requiredWidthToShowSecondary);
    }

    private void ConfigurePassthroughRegions(AppWindow window, double scaleAdjustment)
    {
        if (this.passthroughUnavailable)
        {
            return;
        }

        var newRegions = this.ComputeClampedPassthroughRegions(scaleAdjustment, window.TitleBar.RightInset);

        if (RegionsEqual(this.lastAppliedPassthroughRegions, newRegions))
        {
            this.LogPassthroughRegionsIdentical();
            return;
        }

        try
        {
            var nonClientInputSrc = InputNonClientPointerSource.GetForWindowId(window.Id);
            nonClientInputSrc.SetRegionRects(NonClientRegionKind.Passthrough, [.. newRegions]);
            this.LogPassthroughRegionsSet(newRegions.Length);

            // Store for later comparison
            this.lastAppliedPassthroughRegions = newRegions;
        }
#pragma warning disable CA1031 // Handle API failures gracefully; log and fallback without crashing design/debug sessions.
        catch (Exception ex)
#pragma warning restore CA1031
        {
            this.passthroughUnavailable = true;
            this.lastAppliedPassthroughRegions = null;
            this.LogPassthroughRegionsFailed(ex);
        }

        static bool RegionsEqual(RectInt32[]? a, RectInt32[]? b)
        {
            if (ReferenceEquals(a, b))
            {
                return true;
            }

            if (a is null && b is null)
            {
                return true;
            }

            if (a is null || b is null)
            {
                return false;
            }

            if (a.Length != b.Length)
            {
                return false;
            }

            for (var i = 0; i < a.Length; ++i)
            {
                var x = a[i];
                var y = b[i];
                if (x.X != y.X || x.Y != y.Y || x.Width != y.Width || x.Height != y.Height)
                {
                    return false;
                }
            }

            return true;
        }
    }

    private RectInt32[] ComputeClampedPassthroughRegions(double scaleAdjustment, int systemRightInsetDevice)
    {
        var passthroughRegions = new List<RectInt32>();
        AddPassthroughRegion(this.PrimaryCommands, scaleAdjustment, passthroughRegions);
        AddPassthroughRegion(this.SecondaryCommands, scaleAdjustment, passthroughRegions);

        var clamped = new List<RectInt32>();
        var deviceWindowWidth = (int)Math.Round(this.ActualWidth * scaleAdjustment);
        var gap = Math.Max(2, (int)this.SecondaryCommands.Margin.Right);
        var allowedMaxX = deviceWindowWidth - systemRightInsetDevice - gap;

        foreach (var r in passthroughRegions)
        {
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
                var cr = new RectInt32(_X: r.X, _Y: r.Y, _Width: clampedWidth, _Height: r.Height);
                clamped.Add(cr);
            }
        }

        this.LogComputedPassthroughRegions(passthroughRegions.Count, clamped);

        return [.. clamped];

        void AddPassthroughRegion(
           FrameworkElement element,
           double scaleAdjustment,
           List<RectInt32> regions)
        {
            if (element.Visibility is not Visibility.Visible || element.ActualWidth <= 0)
            {
                // Report skipped interactive element for troubleshooting
                this.LogPassthroughElementSkipped(element.Name ?? element.GetType().Name);
                return;
            }

            var transform = element.TransformToVisual(visual: null);
            var bounds = transform.TransformBounds(new Rect(0, 0, element.ActualWidth, element.ActualHeight));

            var scaledRect = MakeScaledPassthroughRegion(bounds, scaleAdjustment);
            regions.Add(scaledRect);

            // Converts a <see cref="Rect"/> to a <see cref="Windows.Graphics.RectInt32"/> based on the specified scale.
            static RectInt32 MakeScaledPassthroughRegion(Rect bounds, double scale)
               => new()
               {
                   X = (int)Math.Round(bounds.X * scale),
                   Y = (int)Math.Round(bounds.Y * scale),
                   Width = (int)Math.Round(bounds.Width * scale),
                   Height = (int)Math.Round(bounds.Height * scale),
               };
        }
    }
}
