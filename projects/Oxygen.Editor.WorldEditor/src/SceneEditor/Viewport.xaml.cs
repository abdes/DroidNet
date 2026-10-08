// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Runtime.InteropServices;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;
using Windows.System;
using Windows.UI.Core;
using DispatcherQueueTimer = Microsoft.UI.Dispatching.DispatcherQueueTimer;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// A control that displays a 3D viewport with overlay controls.
/// </summary>
public sealed partial class Viewport : UserControl, IAsyncDisposable // TODO: xaml.cs is doing too much instead of the view model
{
    private const string LoggerCategoryName = "Oxygen.Editor.LevelEditor.Viewport";
    private const uint MinimumPixelExtent = 2;
    private const double CompactWidth = 600;
    private const double NarrowWidth = 400;

    // A left press selects; dragging it further than this, in DIPs, draws a marquee instead.
    private const double MarqueeThreshold = 4;

    // The square, in physical pixels, a click picks around the pointer.
    private const uint ClickPickExtent = 7;

    private static readonly TimeSpan StatisticsInterval = TimeSpan.FromMilliseconds(500);

    private static readonly bool EnableInputDebugLogs =
        string.Equals(
            Environment.GetEnvironmentVariable("OXYGEN_VIEWPORT_INPUT_LOGS"),
            "1",
            StringComparison.Ordinal);

    private static readonly bool EnableWheelDebugLogs =
        string.Equals(
            Environment.GetEnvironmentVariable("OXYGEN_VIEWPORT_WHEEL_LOGS"),
            "1",
            StringComparison.Ordinal);

    private IViewportSurfaceLease? surfaceLease;

    // The view model whose view presents through the lease; the control's view model may already
    // have been replaced when the lease is released.
    private ViewportViewModel? leaseViewModel;
    private bool swapChainSizeHooked;

    /* NOTE: composition-scale handling was intentionally removed in favor of
       an explicit, static RenderTransform set in XAML.This keeps the control
       markup predictable and avoids dynamic flicker caused by composition-scale
       oscillation. */

    private int activeLoadCount;
    private CancellationTokenSource? attachCancellationSource;
    private ILogger logger = NullLoggerFactory.Instance.CreateLogger(LoggerCategoryName);
    private ViewportViewModel? currentViewModel;
    private bool isDisposed;
    private long attachRequestId;

    // Rx subject & subscription used to debounce SwapChainPanel size changes
    // at the UI level. Using System.Reactive.Throttle ensures we do not hammer
    // the engine with rapid resize requests originating from layout passes.
    private Subject<Microsoft.UI.Xaml.SizeChangedEventArgs>? sizeChangedSubject;
    private IDisposable? sizeChangedSubscription;

    private bool inputHandlersHooked;
    private Vector2 lastPointerPosition;
    private bool hasPointerPosition;

    private bool lastAltKeyDown;
    private DispatcherQueueTimer? statisticsTimer;

    private Windows.Foundation.Point? selectionStart;
    private ViewportSelectionMode selectionMode;
    private bool isMarquee;

    /// <summary>
    /// Initializes a new instance of the <see cref="Viewport"/> class.
    /// </summary>
    public Viewport()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;

        this.LostFocus += this.OnLostFocus;
        this.KeyDown += this.OnKeyDown;
        this.KeyUp += this.OnKeyUp;

        if (this.DataContext is ViewportViewModel existingViewModel)
        {
            this.currentViewModel = existingViewModel;
            this.OnViewModelChanged(previous: null, existingViewModel);
        }
    }

    /// <summary>
    /// Gets or sets the ViewModel for this viewport.
    /// </summary>
    public ViewportViewModel? ViewModel
    {
        get => this.currentViewModel ?? this.DataContext as ViewportViewModel;
        set
        {
            if (this.isDisposed)
            {
                return;
            }

            this.DataContext = value;
        }
    }

    /// <inheritdoc/>
    public async ValueTask DisposeAsync()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;

        this.Loaded -= this.OnLoaded;
        this.Unloaded -= this.OnUnloaded;
        this.DataContextChanged -= this.OnDataContextChanged;

        this.LostFocus -= this.OnLostFocus;
        this.KeyDown -= this.OnKeyDown;
        this.KeyUp -= this.OnKeyUp;

        this.UnregisterInputHandlers();

        this.UnregisterSwapChainPanelSizeChanged();
        this.StopStatisticsTimer();

        // No theme listeners to remove; view does not interfere with theme settings.
        await this.DetachSurfaceAsync("Dispose").ConfigureAwait(true);
        await this.CancelPendingAttachAsync().ConfigureAwait(true);

        this.currentViewModel = null;
        GC.SuppressFinalize(this);
    }

    private static string GetViewportId(ViewportViewModel? viewModel)
        => viewModel?.ViewportId.ToString("D", CultureInfo.InvariantCulture) ?? "none";

    private static bool TryTranslateMouseButton(PointerUpdateKind kind, out RuntimeMouseButton button, out bool pressed)
    {
        button = RuntimeMouseButton.None;
        pressed = false;

        switch (kind)
        {
            case PointerUpdateKind.LeftButtonPressed:
                button = RuntimeMouseButton.Left;
                pressed = true;
                return true;
            case PointerUpdateKind.LeftButtonReleased:
                button = RuntimeMouseButton.Left;
                pressed = false;
                return true;
            case PointerUpdateKind.RightButtonPressed:
                button = RuntimeMouseButton.Right;
                pressed = true;
                return true;
            case PointerUpdateKind.RightButtonReleased:
                button = RuntimeMouseButton.Right;
                pressed = false;
                return true;
            case PointerUpdateKind.MiddleButtonPressed:
                button = RuntimeMouseButton.Middle;
                pressed = true;
                return true;
            case PointerUpdateKind.MiddleButtonReleased:
                button = RuntimeMouseButton.Middle;
                pressed = false;
                return true;
            case PointerUpdateKind.XButton1Pressed:
                button = RuntimeMouseButton.ExtButton1;
                pressed = true;
                return true;
            case PointerUpdateKind.XButton1Released:
                button = RuntimeMouseButton.ExtButton1;
                pressed = false;
                return true;
            case PointerUpdateKind.XButton2Pressed:
                button = RuntimeMouseButton.ExtButton2;
                pressed = true;
                return true;
            case PointerUpdateKind.XButton2Released:
                button = RuntimeMouseButton.ExtButton2;
                pressed = false;
                return true;
            default:
                return false;
        }
    }

    private void DebugInputLog(string message)
    {
        if (!EnableInputDebugLogs)
        {
            return;
        }

        var viewportId = this.ViewModel?.ViewportId;
        var viewId = this.ViewModel?.AssignedViewId;
        Debug.WriteLine($"[Viewport] vm.viewportId={viewportId} viewId={viewId} :: {message}");
    }

    private void DebugWheelLog(string message)
    {
        if (!EnableWheelDebugLogs)
        {
            return;
        }

        var viewportId = this.ViewModel?.ViewportId;
        var viewId = this.ViewModel?.AssignedViewId;
        Debug.WriteLine($"[Viewport.Wheel] vm.viewportId={viewportId} viewId={viewId} :: {message}");
    }

    private void SyncAltKeyStateIfNeeded(ViewportViewModel viewModel, RuntimeViewTarget viewId)
    {
        var is_down = InputKeyboardSource
            .GetKeyStateForCurrentThread(VirtualKey.Menu)
            .HasFlag(CoreVirtualKeyStates.Down);

        if (is_down == this.lastAltKeyDown)
        {
            return;
        }

        this.lastAltKeyDown = is_down;
        var position = this.hasPointerPosition ? this.lastPointerPosition : Vector2.Zero;

        viewModel.ForwardInput(viewId, new RuntimeKeyEvent(RuntimeKey.LeftAlt, is_down, Repeat: false, position, DateTime.UtcNow));
    }

    private void OnDataContextChanged(FrameworkElement sender, Microsoft.UI.Xaml.DataContextChangedEventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        _ = sender;
        var previous = this.currentViewModel;
        var current = args.NewValue as ViewportViewModel;
        if (ReferenceEquals(previous, current))
        {
            return;
        }

        this.currentViewModel = current;
        this.Bindings.Update();
        this.OnViewModelChanged(previous, current);
    }

    private void OnViewModelChanged(ViewportViewModel? previous, ViewportViewModel? current)
    {
        if (this.isDisposed)
        {
            return;
        }

        this.RefreshLogger(current);
        this.LogViewModelChanged(GetViewportId(previous), GetViewportId(current));

        _ = this.HandleViewModelChangeAsync(previous, current);
    }

    private void OnCameraFlyoutOpening(object? sender, object e) => this.ViewModel?.RefreshSceneCameras();

    private void OnFlyoutChoiceClick(object sender, RoutedEventArgs e)
    {
        // A choice applies at once; the flyout closes like a menu.
        this.CameraFlyout.Hide();
        this.ViewModeFlyout.Hide();
        this.LayoutFlyout.Hide();
    }

    private void OnCameraNumberBoxValidate(object? sender, ValidationEventArgs<float> e)
    {
        if (sender is FrameworkElement { Tag: ViewportCameraNumberBoxItemModel model } && e.NewValue is { } value)
        {
            e.IsValid = model.IsInRange(value);
        }
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs e)
    {
        // Labels go first; a narrow pane then folds Show and Layout into Viewport settings.
        var state = e.NewSize.Width < NarrowWidth ? "Narrow" : e.NewSize.Width < CompactWidth ? "Compact" : "Wide";
        _ = VisualStateManager.GoToState(this, state, useTransitions: false);
    }

    private void OnSettingsFlyoutOpening(object? sender, object e)
    {
        if (sender is not MenuFlyout menu || this.ViewModel is not { } viewModel)
        {
            return;
        }

        menu.Items.Clear();
        menu.Items.Add(CreateToggle("Grid", viewModel.ShowGrid, value => viewModel.ShowGrid = value));
        menu.Items.Add(CreateToggle("Selection outline", viewModel.ShowSelectionOutline, value => viewModel.ShowSelectionOutline = value));
        menu.Items.Add(CreateToggle("Camera preview", viewModel.ShowCameraPreview, value => viewModel.ShowCameraPreview = value));
        menu.Items.Add(CreateToggle("Statistics", viewModel.ShowStatistics, value => viewModel.ShowStatistics = value));

        var layouts = new MenuFlyoutSubItem { Text = "Layout" };
        foreach (var group in viewModel.LayoutGroups)
        {
            if (layouts.Items.Count > 0)
            {
                layouts.Items.Add(new MenuFlyoutSeparator());
            }

            foreach (var option in group.Options)
            {
                layouts.Items.Add(new RadioMenuFlyoutItem
                {
                    Text = $"{group.Title} · {option.Label}",
                    GroupName = "ViewportLayout",
                    IsChecked = option.IsSelected,
                    Command = option.ChooseCommand,
                });
            }
        }

        menu.Items.Add(new MenuFlyoutSeparator());
        menu.Items.Add(layouts);

        static ToggleMenuFlyoutItem CreateToggle(string text, bool isChecked, Action<bool> apply)
        {
            var item = new ToggleMenuFlyoutItem { Text = text, IsChecked = isChecked };
            item.Click += (_, _) => apply(item.IsChecked);
            return item;
        }
    }

    private void StartStatisticsTimer()
    {
        if (this.statisticsTimer is null)
        {
            this.statisticsTimer = this.DispatcherQueue.CreateTimer();
            this.statisticsTimer.Interval = StatisticsInterval;
            this.statisticsTimer.Tick += this.OnStatisticsTick;
        }

        this.statisticsTimer.Start();
    }

    private void StopStatisticsTimer() => this.statisticsTimer?.Stop();

    private void OnStatisticsTick(DispatcherQueueTimer sender, object args)
    {
        if (this.ViewModel is { ShowStatistics: true } viewModel)
        {
            viewModel.RefreshStatistics();
        }
    }

    private async Task HandleViewModelChangeAsync(ViewportViewModel? previous, ViewportViewModel? current)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (previous != null)
        {
            await this.DetachSurfaceAsync("ViewModelChanged").ConfigureAwait(true);
        }

        if (current != null && this.IsLoaded && this.surfaceLease == null)
        {
            await this.AttachSurfaceAsync("ViewModelChanged").ConfigureAwait(true);
        }
    }

    private void RefreshLogger(ViewportViewModel? viewModel)
    {
        var factory = viewModel?.LoggerFactory ?? NullLoggerFactory.Instance;
        this.logger = factory.CreateLogger(LoggerCategoryName);
    }

    private void OnLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        if (this.isDisposed)
        {
            return;
        }

        _ = sender;
        _ = e;

        this.LogLoadedInvoked(this.activeLoadCount);
        this.activeLoadCount++;
        this.LogLoadCountChanged(this.activeLoadCount);

        if (this.surfaceLease != null)
        {
            this.LogSurfaceAlreadyAttachedOnLoad();
            _ = this.NotifyViewportResizeAsync(CancellationToken.None);
            return;
        }

        if (!this.swapChainSizeHooked)
        {
            this.SwapChainPanel.SizeChanged += this.OnSwapChainPanelSizeChanged;

            // Also observe composition-scale changes (compositor-driven transform)
            // as recommended by the SwapChainPanel docs. This lets us detect when
            // the compositor applies additional scaling (e.g. transforms or dpi)
            // and re-request an appropriate resize so the native backbuffers and
            // engine configuration stay in sync with presented pixels.
            // CompositionScaleChanged handling removed - use static XAML RenderTransform instead
            this.sizeChangedSubject ??= new Subject<SizeChangedEventArgs>();

            var dispatcherScheduler = new DispatcherQueueScheduler(
                this.DispatcherQueue); // WinUI 3 dispatcher

            this.sizeChangedSubscription ??= this.sizeChangedSubject

                // Use the dispatcher scheduler for throttle timing so debounce and callbacks
                // are scheduled on the UI dispatcher. This avoids cross-thread timing races
                // and ensures the factory passed to FromAsync runs on the dispatcher.
                .Throttle(TimeSpan.FromMilliseconds(150), dispatcherScheduler)
                .ObserveOn(dispatcherScheduler)

                // Use Switch to cancel any previous in-flight resize when a new
                // debounced event arrives. Each inner observable is created from
                // the async resize method and receives the Rx cancellation token.
                .Select(_ => Observable.FromAsync(ct => this.NotifyViewportResizeAsync(ct)))
                .Switch()
                .Subscribe(
                    _ => { },
                    this.LogResizeFailed);

            this.swapChainSizeHooked = true;
            this.LogSwapChainHookRegistered();
        }

        this.RegisterInputHandlers();
        this.StartStatisticsTimer();

        _ = this.AttachSurfaceAsync("Loaded");
    }

    private void OnSwapChainPanelSizeChanged(object sender, Microsoft.UI.Xaml.SizeChangedEventArgs e)
    {
        if (this.isDisposed)
        {
            return;
        }

        _ = sender;
        this.LogSwapChainSizeChanged(e.NewSize.Width, e.NewSize.Height);

        // Push event into the debounced pipeline; if Rx setup failed we still
        // have OnSwapChainPanelSizeChanged called and want to behave like before.
        if (this.sizeChangedSubject != null)
        {
            try
            {
                this.sizeChangedSubject.OnNext(e);
            }
            catch (ObjectDisposedException)
            {
                // Subscription/subject disposed concurrently; ignore safely.
            }
            catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                this.LogResizeFailed(ex);
            }
        }
        else
        {
            _ = this.NotifyViewportResizeAsync(CancellationToken.None);
        }
    }

    private async Task NotifyViewportResizeAsync(CancellationToken cancellationToken = default)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (this.surfaceLease == null)
        {
            this.LogResizeSkipped("No active surface lease");
            return;
        }

        if (!this.TryGetSwapChainPixelSize(out var pixelWidth, out var pixelHeight))
        {
            return;
        }

        if (pixelWidth < MinimumPixelExtent || pixelHeight < MinimumPixelExtent)
        {
            this.LogResizeSkipped("Computed pixel size below minimum threshold");
            return;
        }

        try
        {
            this.LogViewportResizing(pixelWidth, pixelHeight);
            await this.surfaceLease.ResizeAsync(pixelWidth, pixelHeight, cancellationToken).ConfigureAwait(true);
            this.LogViewportResized(pixelWidth, pixelHeight);
        }
        catch (OperationCanceledException)
        {
            this.LogResizeSkipped("Resize canceled");
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogResizeFailed(ex);
            this.ViewModel?.PublishRuntimeWarning(
                RuntimeOperationKinds.SurfaceResize,
                FailureDomain.RuntimeSurface,
                DiagnosticCodes.SurfacePrefix + "RESIZE_FAILED",
                "Viewport resize failed",
                "The viewport surface could not be resized.",
                ex);
        }
    }

    private async void OnUnloaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        if (this.isDisposed)
        {
            return;
        }

        _ = sender;
        _ = e;

        this.LogUnloadedInvoked(this.activeLoadCount);
        if (this.activeLoadCount > 0)
        {
            this.activeLoadCount--;
            this.LogLoadCountChanged(this.activeLoadCount);
        }

        if (this.activeLoadCount > 0)
        {
            this.LogUnloadIgnored(this.activeLoadCount);
            return;
        }

        this.UnregisterSwapChainPanelSizeChanged();

        this.UnregisterInputHandlers();
        this.StopStatisticsTimer();

        await this.DetachSurfaceAsync("ActiveLoadCountZero").ConfigureAwait(true);
    }

    private void RegisterInputHandlers()
    {
        if (this.inputHandlersHooked)
        {
            this.DebugInputLog("RegisterInputHandlers: already hooked");
            return;
        }

        if (this.SwapChainPanel == null)
        {
            this.DebugInputLog("RegisterInputHandlers: SwapChainPanel is null");
            return;
        }

        this.IsTabStop = true;

        // Ensure this element participates in hit-testing.
        this.SwapChainPanel.IsHitTestVisible = true;

        this.SwapChainPanel.PointerPressed += this.OnSwapChainPointerPressed;
        this.SwapChainPanel.PointerReleased += this.OnSwapChainPointerReleased;
        this.SwapChainPanel.PointerMoved += this.OnSwapChainPointerMoved;
        this.SwapChainPanel.PointerWheelChanged += this.OnSwapChainPointerWheelChanged;

        this.inputHandlersHooked = true;
        this.DebugInputLog("RegisterInputHandlers: hooked SwapChainPanel pointer events");
    }

    private void UnregisterInputHandlers()
    {
        if (!this.inputHandlersHooked)
        {
            return;
        }

        if (this.SwapChainPanel != null)
        {
            this.SwapChainPanel.PointerPressed -= this.OnSwapChainPointerPressed;
            this.SwapChainPanel.PointerReleased -= this.OnSwapChainPointerReleased;
            this.SwapChainPanel.PointerMoved -= this.OnSwapChainPointerMoved;
            this.SwapChainPanel.PointerWheelChanged -= this.OnSwapChainPointerWheelChanged;
        }

        this.inputHandlersHooked = false;
        this.hasPointerPosition = false;

        this.DebugInputLog("UnregisterInputHandlers: unhooked");
    }

    private bool TryGetInputTarget(out ViewportViewModel? viewModel, out RuntimeViewTarget viewId)
    {
        viewModel = this.ViewModel;
        viewId = viewModel?.AssignedInputTarget ?? default;
        return viewModel?.AssignedInputTarget is not null;
    }

    private bool TryGetInputTargetVerbose(out ViewportViewModel? viewModel, out RuntimeViewTarget viewId)
    {
        viewModel = this.ViewModel;
        viewId = viewModel?.AssignedInputTarget ?? default;

        if (viewModel is null)
        {
            this.DebugInputLog("Input target missing: ViewModel is null");
            return false;
        }

        if (viewModel.EngineService is null)
        {
            this.DebugInputLog("Input target missing: EngineService is null");
            return false;
        }

        if (viewModel.AssignedInputTarget is null)
        {
            this.DebugInputLog("Input target missing: EngineService.Input is null");
            return false;
        }

        if (viewId.Generation == Guid.Empty)
        {
            this.DebugInputLog("Input target missing: AssignedViewId is invalid");
            return false;
        }

        return true;
    }

    private void PushFocusLostIfNeeded()
    {
        if (this.isDisposed)
        {
            this.DebugInputLog("FocusLost: disposed");
            return;
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("FocusLost: no input target");
            return;
        }

        this.DebugInputLog("FocusLost: forwarding to EngineService.Input.OnFocusLost");
        viewModel.ForwardInput(viewId, new RuntimeFocusLostEvent());
    }

    private void OnLostFocus(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.CancelSelectionGesture();
        this.PushFocusLostIfNeeded();
    }

    /// <summary>
    /// Starts a selection gesture on a left press without Alt: Alt+drag stays navigation. Ctrl
    /// toggles the picked nodes, Shift adds them.
    /// </summary>
    private void BeginSelectionGesture(PointerPoint point, VirtualKeyModifiers modifiers)
    {
        this.CancelSelectionGesture();
        if (point.Properties.PointerUpdateKind != PointerUpdateKind.LeftButtonPressed
            || modifiers.HasFlag(VirtualKeyModifiers.Menu) || this.lastAltKeyDown)
        {
            return;
        }

        this.selectionStart = point.Position;
        this.selectionMode = modifiers.HasFlag(VirtualKeyModifiers.Control)
            ? ViewportSelectionMode.Toggle
            : modifiers.HasFlag(VirtualKeyModifiers.Shift) ? ViewportSelectionMode.Add : ViewportSelectionMode.Replace;
    }

    private void UpdateSelectionGesture(PointerPoint point)
    {
        if (this.selectionStart is not { } start)
        {
            return;
        }

        var position = point.Position;
        if (!this.isMarquee)
        {
            var dx = position.X - start.X;
            var dy = position.Y - start.Y;
            if ((dx * dx) + (dy * dy) <= MarqueeThreshold * MarqueeThreshold)
            {
                return;
            }

            this.isMarquee = true;
            this.MarqueeRectangle.Visibility = Visibility.Visible;
        }

        // The overlay canvas shares the SwapChainPanel's DIPs.
        Canvas.SetLeft(this.MarqueeRectangle, Math.Min(start.X, position.X));
        Canvas.SetTop(this.MarqueeRectangle, Math.Min(start.Y, position.Y));
        this.MarqueeRectangle.Width = Math.Abs(position.X - start.X);
        this.MarqueeRectangle.Height = Math.Abs(position.Y - start.Y);
    }

    private void CompleteSelectionGesture(ViewportViewModel viewModel, PointerPoint point)
    {
        if (this.selectionStart is not { } start || point.Properties.PointerUpdateKind != PointerUpdateKind.LeftButtonReleased)
        {
            return;
        }

        var isMarquee = this.isMarquee;
        var mode = this.selectionMode;
        this.CancelSelectionGesture();

        // Picks address the surface in physical pixels.
        var scale = this.XamlRoot?.RasterizationScale ?? 1.0;
        RuntimePickRect rect;
        if (isMarquee)
        {
            var left = Math.Max(0.0, Math.Min(start.X, point.Position.X) * scale);
            var top = Math.Max(0.0, Math.Min(start.Y, point.Position.Y) * scale);
            var right = Math.Max(start.X, point.Position.X) * scale;
            var bottom = Math.Max(start.Y, point.Position.Y) * scale;
            rect = new RuntimePickRect(
                (uint)left,
                (uint)top,
                (uint)Math.Max(1.0, Math.Ceiling(right - left)),
                (uint)Math.Max(1.0, Math.Ceiling(bottom - top)));
        }
        else
        {
            const int halfExtent = (int)(ClickPickExtent / 2);
            var x = (int)(point.Position.X * scale) - halfExtent;
            var y = (int)(point.Position.Y * scale) - halfExtent;
            rect = new RuntimePickRect((uint)Math.Max(0, x), (uint)Math.Max(0, y), ClickPickExtent, ClickPickExtent);
        }

        _ = viewModel.PickAsync(rect, mode, isMarquee);
    }

    private void CancelSelectionGesture()
    {
        this.selectionStart = null;
        this.isMarquee = false;
        this.MarqueeRectangle.Visibility = Visibility.Collapsed;
    }

    /// <summary>Handles the pane's own keys: F frames the selection, Shift+F the whole scene, Escape cancels a marquee.</summary>
    /// <returns><see langword="true"/> when the key was handled and must not reach the engine.</returns>
    private bool TryHandleSelectionKey(ViewportViewModel viewModel, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Escape && this.selectionStart is not null)
        {
            this.CancelSelectionGesture();
            return true;
        }

        if (e.Key != VirtualKey.F || e.KeyStatus.RepeatCount > 1
            || IsKeyDown(VirtualKey.Control) || IsKeyDown(VirtualKey.Menu))
        {
            return false;
        }

        _ = IsKeyDown(VirtualKey.Shift) ? viewModel.FrameAllAsync() : viewModel.FrameSelectionAsync();
        return true;

        static bool IsKeyDown(VirtualKey key)
            => InputKeyboardSource.GetKeyStateForCurrentThread(key).HasFlag(CoreVirtualKeyStates.Down);
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        this.DebugInputLog($"KeyDown: key={e.Key} repeatCount={e.KeyStatus.RepeatCount}");

        if (this.isDisposed)
        {
            this.DebugInputLog("KeyDown: disposed");
            return;
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("KeyDown: no input target");
            return;
        }

        // Prevent Alt (Menu) from being treated as a system menu activation.
        if (e.Key == VirtualKey.Menu)
        {
            e.Handled = true;
        }

        if (this.TryHandleSelectionKey(viewModel, e))
        {
            e.Handled = true;
            return;
        }

        var translated = InputTranslation.TranslateKey((VirtualKey)e.Key);
        if (translated == RuntimeKey.None)
        {
            this.DebugInputLog("KeyDown: TranslateKey returned None");
            return;
        }

        var position = this.hasPointerPosition ? this.lastPointerPosition : Vector2.Zero;
        this.DebugInputLog($"KeyDown: forwarding key={translated} pressed=true");

        if (translated == RuntimeKey.LeftAlt)
        {
            this.lastAltKeyDown = true;
        }

        viewModel.ForwardInput(viewId, new RuntimeKeyEvent(translated, Pressed: true, e.KeyStatus.RepeatCount > 1, position, DateTime.UtcNow));
    }

    private void OnKeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        this.DebugInputLog($"KeyUp: key={e.Key}");

        if (this.isDisposed)
        {
            this.DebugInputLog("KeyUp: disposed");
            return;
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("KeyUp: no input target");
            return;
        }

        if (e.Key == VirtualKey.Menu)
        {
            e.Handled = true;
        }

        var translated = InputTranslation.TranslateKey((VirtualKey)e.Key);
        if (translated == RuntimeKey.None)
        {
            this.DebugInputLog("KeyUp: TranslateKey returned None");
            return;
        }

        var position = this.hasPointerPosition ? this.lastPointerPosition : Vector2.Zero;
        this.DebugInputLog($"KeyUp: forwarding key={translated} pressed=false");

        if (translated == RuntimeKey.LeftAlt)
        {
            this.lastAltKeyDown = false;
        }

        viewModel.ForwardInput(viewId, new RuntimeKeyEvent(translated, Pressed: false, Repeat: false, position, DateTime.UtcNow));
    }

    private void OnSwapChainPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        _ = sender;

        this.DebugInputLog($"PointerPressed: pointerId={e.Pointer.PointerId}");

        if (this.isDisposed)
        {
            this.DebugInputLog("PointerPressed: disposed");
            return;
        }

        _ = this.Focus(FocusState.Pointer);

        _ = this.SwapChainPanel?.CapturePointer(e.Pointer);

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("PointerPressed: no input target");
            return;
        }

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        this.DebugInputLog(string.Create(CultureInfo.InvariantCulture, $"PointerPressed: pos=({point.Position.X:0.0},{point.Position.Y:0.0}) updateKind={point.Properties.PointerUpdateKind}"));
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y) * (float)(this.XamlRoot?.RasterizationScale ?? 1.0);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        this.BeginSelectionGesture(point, e.KeyModifiers);

        var kind = point.Properties.PointerUpdateKind;
        if (!TryTranslateMouseButton(kind, out var button, out var pressed) || !pressed)
        {
            button = InputTranslation.TranslateMouseButton(point.Properties);
            pressed = true;
        }

        if (button == RuntimeMouseButton.None)
        {
            this.DebugInputLog("PointerPressed: could not determine button");
            return;
        }

        this.DebugInputLog($"PointerPressed: forwarding button={button} pressed={pressed}");
        viewModel.ForwardInput(viewId, new RuntimeButtonEvent(button, pressed, pos, DateTime.UtcNow));
    }

    private void OnSwapChainPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        _ = sender;

        this.DebugInputLog($"PointerReleased: pointerId={e.Pointer.PointerId}");

        if (this.isDisposed)
        {
            this.DebugInputLog("PointerReleased: disposed");
            return;
        }

        this.SwapChainPanel?.ReleasePointerCapture(e.Pointer);

        // CRITICAL: Restore keyboard focus after releasing pointer capture
        // ReleasePointerCapture causes WinUI to stop routing keyboard events!
        // Must defer focus call to next message loop cycle - immediate Focus() fails.
        _ = this.DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.High, () => _ = this.Focus(FocusState.Keyboard));

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("PointerReleased: no input target");
            return;
        }

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        this.DebugInputLog(string.Create(CultureInfo.InvariantCulture, $"PointerReleased: pos=({point.Position.X:0.0},{point.Position.Y:0.0}) updateKind={point.Properties.PointerUpdateKind}"));
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y) * (float)(this.XamlRoot?.RasterizationScale ?? 1.0);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        this.CompleteSelectionGesture(viewModel, point);

        var kind = point.Properties.PointerUpdateKind;
        if (!TryTranslateMouseButton(kind, out var button, out var pressed))
        {
            button = InputTranslation.TranslateMouseButton(point.Properties);
            pressed = false;
        }

        if (button == RuntimeMouseButton.None)
        {
            this.DebugInputLog("PointerReleased: could not determine button");
            return;
        }

        this.DebugInputLog($"PointerReleased: forwarding button={button} pressed={pressed}");
        viewModel.ForwardInput(viewId, new RuntimeButtonEvent(button, pressed, pos, DateTime.UtcNow));
    }

    private void OnSwapChainPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        _ = sender;

        if (this.isDisposed)
        {
            return;
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            return;
        }

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        this.UpdateSelectionGesture(point);
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y) * (float)(this.XamlRoot?.RasterizationScale ?? 1.0);
        var delta =this.hasPointerPosition ? (pos - this.lastPointerPosition) : Vector2.Zero;

        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        if (delta != Vector2.Zero)
        {
            this.DebugInputLog(string.Create(CultureInfo.InvariantCulture, $"PointerMoved: pos=({pos.X:0.0},{pos.Y:0.0}) delta=({delta.X:0.0},{delta.Y:0.0})"));
        }

        viewModel.ForwardInput(viewId, new RuntimeMouseMotionEvent(delta, pos, DateTime.UtcNow));
    }

    private void OnSwapChainPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        _ = sender;

        this.DebugInputLog($"PointerWheelChanged: pointerId={e.Pointer.PointerId}");

        if (this.isDisposed)
        {
            this.DebugInputLog("PointerWheelChanged: disposed");
            return;
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("PointerWheelChanged: no input target");
            return;
        }

        // Wheel navigation makes its pane the focused one, like any other pointer navigation.
        _ = this.Focus(FocusState.Pointer);
        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y) * (float)(this.XamlRoot?.RasterizationScale ?? 1.0);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        var rawDelta = point.Properties.MouseWheelDelta;
        var ticks = rawDelta / 120.0f;
        if (Math.Abs(ticks) <= float.Epsilon)
        {
            this.DebugInputLog("PointerWheelChanged: zero delta");
            this.DebugWheelLog(string.Create(CultureInfo.InvariantCulture, $"rawDelta={rawDelta} ticks={ticks:0.00} (ignored: zero)"));
            return;
        }

        this.DebugWheelLog(
            string.Create(CultureInfo.InvariantCulture, $"rawDelta={rawDelta} ticks={ticks:0.00} pos=({pos.X:0.0},{pos.Y:0.0})"));

        this.DebugInputLog(string.Create(CultureInfo.InvariantCulture, $"PointerWheelChanged: forwarding ticks={ticks:0.00}"));
        viewModel.ForwardInput(viewId, new RuntimeMouseWheelEvent(new Vector2(0.0f, (float)ticks), pos, DateTime.UtcNow));
    }

    private async Task AttachSurfaceAsync(string reason = "General")
    {
        if (this.isDisposed)
        {
            return;
        }

        this.LogAttachRequested(reason);

        var viewModel = this.ViewModel;
        if (viewModel?.EngineService == null)
        {
            this.LogMissingEngineService();
            return;
        }

        if (this.SwapChainPanel == null)
        {
            this.LogSwapChainPanelNotReady();
            return;
        }

        await this.CancelPendingAttachAsync().ConfigureAwait(true);
        this.attachCancellationSource = new CancellationTokenSource();
        var cancellationToken = this.attachCancellationSource.Token;
        var requestId = Interlocked.Increment(ref this.attachRequestId);

        try
        {
            await this.WaitForPanelMeasurementAsync(cancellationToken).ConfigureAwait(true);

            await this.AttachMeasuredSurfaceAsync(viewModel, requestId, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            this.LogAttachmentCanceled(reason);
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogAttachmentFailed(ex);
            viewModel.PublishRuntimeFailure(
                RuntimeOperationKinds.SurfaceAttach,
                FailureDomain.RuntimeSurface,
                DiagnosticCodes.SurfacePrefix + "ATTACH_FAILED",
                "Viewport surface attachment failed",
                "The runtime could not attach the viewport surface.",
                ex);
        }
    }

    private async Task DetachSurfaceAsync(string reason = "General")
    {
        if (this.isDisposed && !string.Equals(reason, "Dispose", StringComparison.Ordinal))
        {
            return;
        }

        this.LogDetachRequested(reason);
        await this.CancelPendingAttachAsync().ConfigureAwait(true);

        if (this.surfaceLease == null)
        {
            return;
        }

        try
        {
            // The pane keeps its camera state for the view it creates on its next attach.
            if (this.leaseViewModel is { } viewModel)
            {
                await viewModel.ReleaseViewAsync().ConfigureAwait(true);
            }

            await this.surfaceLease.DisposeAsync().ConfigureAwait(true);
            this.LogLeaseDisposed(GetViewportId(this.leaseViewModel));
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogLeaseDisposeFailed(ex);
        }
        finally
        {
            this.surfaceLease = null;
            this.leaseViewModel = null;
        }
    }

    private void UnregisterSwapChainPanelSizeChanged()
    {
        if (!this.swapChainSizeHooked)
        {
            return;
        }

        if (this.SwapChainPanel is { } panel)
        {
            panel.SizeChanged -= this.OnSwapChainPanelSizeChanged;
        }

        // Stop and dispose the Rx subscription and subject when we detach so
        // we stop dispatching debounced resize calls.
        try
        {
            this.sizeChangedSubscription?.Dispose();
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogResizeFailed(ex);
        }

        this.sizeChangedSubscription = null;

        try
        {
            this.sizeChangedSubject?.OnCompleted();
            this.sizeChangedSubject?.Dispose();
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogResizeFailed(ex);
        }

        this.sizeChangedSubject = null;

        this.swapChainSizeHooked = false;
        this.LogSwapChainHookUnregistered();
    }

    private async ValueTask CancelPendingAttachAsync()
    {
        if (this.attachCancellationSource == null)
        {
            return;
        }

        var source = this.attachCancellationSource;
        this.attachCancellationSource = null;

        try
        {
            await source.CancelAsync().ConfigureAwait(false);
        }
        catch (ObjectDisposedException)
        {
            // Already disposed; nothing left to cancel.
        }
        finally
        {
            source.Dispose();
        }
    }

    private bool TryGetSwapChainPixelSize(out uint pixelWidth, out uint pixelHeight)
    {
        pixelWidth = 0;
        pixelHeight = 0;

        if (this.SwapChainPanel == null || this.XamlRoot == null)
        {
            this.LogResizeSkipped("SwapChainPanel or XamlRoot not ready");
            return false;
        }

        try
        {
            var scale = this.XamlRoot.RasterizationScale;
            pixelWidth = (uint)Math.Max(1, Math.Round(this.SwapChainPanel.ActualWidth * scale));
            pixelHeight = (uint)Math.Max(1, Math.Round(this.SwapChainPanel.ActualHeight * scale));
            return true;
        }
        catch (COMException ex)
        {
            this.LogSwapChainAccessFailed(ex);
            return false;
        }
    }

    private async Task DisposeLeaseSilentlyAsync(IViewportSurfaceLease lease)
    {
        try
        {
            await lease.DisposeAsync().ConfigureAwait(true);
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogLeaseDisposeFailed(ex);
        }
    }

    private async Task WaitForPanelMeasurementAsync(CancellationToken cancellationToken)
    {
        // If the SwapChainPanel hasn't been measured yet we can end up
        // registering a 1x1 backbuffer which the engine will use to
        // configure the camera incorrectly. Wait briefly for the panel to
        // be measured so we can pass a realistic initial size to the
        // engine. This is conservative and short-lived; if measurement does
        // not complete we fall back to proceeding immediately.
        const int maxAttempts = 10;
        const int delayMs = 50;
        var attempted = 0;
        while (attempted < maxAttempts && !cancellationToken.IsCancellationRequested)
        {
            if (this.TryGetSwapChainPixelSize(out var w, out var h) && w >= MinimumPixelExtent && h >= MinimumPixelExtent)
            {
                break; // measured to a usable size
            }

            attempted++;
            try
            {
                await Task.Delay(delayMs, cancellationToken).ConfigureAwait(true);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task<bool> KeepAttachedLeaseAsync(ViewportViewModel requestedViewModel, long requestId, IViewportSurfaceLease lease)
    {
        var shouldKeepLease = this.IsLoaded && !this.isDisposed && ReferenceEquals(requestedViewModel, this.ViewModel) && requestId == this.attachRequestId;
        if (!shouldKeepLease)
        {
            this.LogAttachOutcomeIgnored(!this.IsLoaded || this.isDisposed ? "ControlStateChanged" : !ReferenceEquals(requestedViewModel, this.ViewModel) ? "ViewModelChanged" : "SupersededRequest");
            await this.DisposeLeaseSilentlyAsync(lease).ConfigureAwait(true);
            if (requestId == this.attachRequestId)
            {
                await this.CancelPendingAttachAsync().ConfigureAwait(true);
            }

            return false;
        }

        return true;
    }

    private async Task AttachMeasuredSurfaceAsync(ViewportViewModel viewModel, long requestId, CancellationToken cancellationToken)
    {
        // Use a non-empty tag for surface requests — control `Name` is often
        // the empty string (not null), so the null-coalescing operator
        // doesn't help. Treat empty/whitespace as missing and fall back.
        var requestTag = string.IsNullOrWhiteSpace(this.Name) ? "viewport" : this.Name;
        var request = viewModel.CreateSurfaceRequest(requestTag);
        var lease = await viewModel.EngineService.AttachViewportAsync(request, this.SwapChainPanel, cancellationToken).ConfigureAwait(true);

        if (!await this.KeepAttachedLeaseAsync(viewModel, requestId, lease).ConfigureAwait(true))
        {
            return;
        }

        this.surfaceLease = lease;
        this.leaseViewModel = viewModel;
        this.LogSurfaceAttached(viewModel.ViewportId);

        // The view model owns the view: it creates it from the pane's kept camera state.
        _ = this.TryGetSwapChainPixelSize(out var pixelWidth, out var pixelHeight);
        await viewModel.CreateViewAsync(lease.Key.ViewportId, requestTag, pixelWidth, pixelHeight).ConfigureAwait(true);

        // Perform the initial resize unconditionally (do not cancel via the
        // attach token) so the swapchain receives its first backbuffer size.
        // Subsequent resize events are handled by the debounced pipeline and
        // are cancellable.
        await this.NotifyViewportResizeAsync(CancellationToken.None).ConfigureAwait(true);
    }
}
