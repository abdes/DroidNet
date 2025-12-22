// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Runtime.InteropServices;
using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Runtime.Input;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop;
using Oxygen.Interop.Input;
using System.Numerics;
using Windows.System;
using Windows.UI.Core;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// A control that displays a 3D viewport with overlay controls.
/// </summary>
public sealed partial class Viewport : UserControl, IAsyncDisposable // TODO: xaml.cs is doing too much instead of the view model
{
    private const string LoggerCategoryName = "Oxygen.Editor.LevelEditor.Viewport";
    private const uint MinimumPixelExtent = 2;

    private IViewportSurfaceLease? surfaceLease;
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

    private static readonly bool EnableInputDebugLogs =
        string.Equals(
            Environment.GetEnvironmentVariable("OXYGEN_VIEWPORT_INPUT_LOGS"),
            "1",
            StringComparison.Ordinal);

    // Dedicated wheel-only tracing to help verify scroll routing and
    // accumulator correctness without turning on noisy full input logs.
    private static readonly bool EnableWheelDebugLogs =
        string.Equals(
            Environment.GetEnvironmentVariable("OXYGEN_VIEWPORT_WHEEL_LOGS"),
            "1",
            StringComparison.Ordinal);

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

    private void SyncAltKeyStateIfNeeded(ViewportViewModel viewModel, ViewIdManaged viewId)
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

        viewModel.EngineService.Input.PushKeyEvent(
            viewId,
            new EditorKeyEventManaged
            {
                key = PlatformKey.LeftAlt,
                pressed = is_down,
                repeat = false,
                timestamp = DateTime.UtcNow,
                position = position,
            });
    }

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

        // No theme listeners to remove; view does not interfere with theme settings.
        await this.DetachSurfaceAsync("Dispose").ConfigureAwait(true);
        await this.CancelPendingAttachAsync().ConfigureAwait(true);

        this.currentViewModel = null;
        GC.SuppressFinalize(this);
    }

    private static string GetViewportId(ViewportViewModel? viewModel)
        => viewModel?.ViewportId.ToString("D", CultureInfo.InvariantCulture) ?? "none";

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

        // Dispose previous viewmodel subscriptions if present so it can detach
        // from services (appearance settings, etc.). This prevents duplicate
        // subscriptions when swapping viewmodels.
        previous?.Dispose();
        _ = this.HandleViewModelChangeAsync(previous, current);
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
            _ = this.NotifyViewportResizeAsync();
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
                    ex => this.LogResizeFailed(ex));

            this.swapChainSizeHooked = true;
            this.LogSwapChainHookRegistered();
        }

        this.RegisterInputHandlers();

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
            catch (Exception ex)
            {
                this.LogResizeFailed(ex);
            }
        }
        else
        {
            _ = this.NotifyViewportResizeAsync();
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
#pragma warning disable CA1031 // Engine resize failures should not crash the UI; log and continue.
        catch (Exception ex)
        {
            this.LogResizeFailed(ex);
        }
#pragma warning restore CA1031
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

    private static bool TryTranslateMouseButton(PointerUpdateKind kind, out PlatformMouseButton button, out bool pressed)
    {
        button = PlatformMouseButton.None;
        pressed = false;

        switch (kind)
        {
            case PointerUpdateKind.LeftButtonPressed:
                button = PlatformMouseButton.Left;
                pressed = true;
                return true;
            case PointerUpdateKind.LeftButtonReleased:
                button = PlatformMouseButton.Left;
                pressed = false;
                return true;
            case PointerUpdateKind.RightButtonPressed:
                button = PlatformMouseButton.Right;
                pressed = true;
                return true;
            case PointerUpdateKind.RightButtonReleased:
                button = PlatformMouseButton.Right;
                pressed = false;
                return true;
            case PointerUpdateKind.MiddleButtonPressed:
                button = PlatformMouseButton.Middle;
                pressed = true;
                return true;
            case PointerUpdateKind.MiddleButtonReleased:
                button = PlatformMouseButton.Middle;
                pressed = false;
                return true;
            case PointerUpdateKind.XButton1Pressed:
                button = PlatformMouseButton.ExtButton1;
                pressed = true;
                return true;
            case PointerUpdateKind.XButton1Released:
                button = PlatformMouseButton.ExtButton1;
                pressed = false;
                return true;
            case PointerUpdateKind.XButton2Pressed:
                button = PlatformMouseButton.ExtButton2;
                pressed = true;
                return true;
            case PointerUpdateKind.XButton2Released:
                button = PlatformMouseButton.ExtButton2;
                pressed = false;
                return true;
            default:
                return false;
        }
    }

    private bool TryGetInputTarget(out ViewportViewModel? viewModel, out ViewIdManaged viewId)
    {
        viewModel = this.ViewModel;
        viewId = viewModel?.AssignedViewId ?? ViewIdManaged.Invalid;
        return viewModel?.EngineService?.Input != null && viewId.IsValid;
    }

    private bool TryGetInputTargetVerbose(out ViewportViewModel? viewModel, out ViewIdManaged viewId)
    {
        viewModel = this.ViewModel;
        viewId = viewModel?.AssignedViewId ?? ViewIdManaged.Invalid;

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

        if (viewModel.EngineService.Input is null)
        {
            this.DebugInputLog("Input target missing: EngineService.Input is null");
            return false;
        }

        if (!viewId.IsValid)
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
        viewModel.EngineService.Input.OnFocusLost(viewId);
    }

    private void OnLostFocus(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.PushFocusLostIfNeeded();
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

        var translated = InputTranslation.TranslateKey((VirtualKey)e.Key);
        if (translated == PlatformKey.None)
        {
            this.DebugInputLog("KeyDown: TranslateKey returned None");
            return;
        }

        var position = this.hasPointerPosition ? this.lastPointerPosition : Vector2.Zero;
        this.DebugInputLog($"KeyDown: forwarding key={translated} pressed=true");

        if (translated == PlatformKey.LeftAlt)
        {
            this.lastAltKeyDown = true;
        }

        viewModel.EngineService.Input.PushKeyEvent(
            viewId,
            new EditorKeyEventManaged
            {
                key = translated,
                pressed = true,
                repeat = e.KeyStatus.RepeatCount > 1,
                timestamp = DateTime.UtcNow,
                position = position,
            });
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
        if (translated == PlatformKey.None)
        {
            this.DebugInputLog("KeyUp: TranslateKey returned None");
            return;
        }

        var position = this.hasPointerPosition ? this.lastPointerPosition : Vector2.Zero;
        this.DebugInputLog($"KeyUp: forwarding key={translated} pressed=false");

        if (translated == PlatformKey.LeftAlt)
        {
            this.lastAltKeyDown = false;
        }

        viewModel.EngineService.Input.PushKeyEvent(
            viewId,
            new EditorKeyEventManaged
            {
                key = translated,
                pressed = false,
                repeat = false,
                timestamp = DateTime.UtcNow,
                position = position,
            });
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

        if (this.SwapChainPanel != null)
        {
            this.SwapChainPanel.CapturePointer(e.Pointer);
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("PointerPressed: no input target");
            return;
        }

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        this.DebugInputLog($"PointerPressed: pos=({point.Position.X:0.0},{point.Position.Y:0.0}) updateKind={point.Properties.PointerUpdateKind}");
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        var kind = point.Properties.PointerUpdateKind;
        if (!TryTranslateMouseButton(kind, out var button, out var pressed) || !pressed)
        {
            button = InputTranslation.TranslateMouseButton(point.Properties);
            pressed = true;
        }

        if (button == PlatformMouseButton.None)
        {
            this.DebugInputLog("PointerPressed: could not determine button");
            return;
        }

        this.DebugInputLog($"PointerPressed: forwarding button={button} pressed={pressed}");
        viewModel.EngineService.Input.PushButtonEvent(
            viewId,
            new EditorButtonEventManaged
            {
                button = button,
                pressed = pressed,
                timestamp = DateTime.UtcNow,
                position = pos,
            });
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

        if (this.SwapChainPanel != null)
        {
            this.SwapChainPanel.ReleasePointerCapture(e.Pointer);
        }

        if (!this.TryGetInputTargetVerbose(out var viewModel, out var viewId) || viewModel is null)
        {
            this.DebugInputLog("PointerReleased: no input target");
            return;
        }

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        this.DebugInputLog($"PointerReleased: pos=({point.Position.X:0.0},{point.Position.Y:0.0}) updateKind={point.Properties.PointerUpdateKind}");
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        var kind = point.Properties.PointerUpdateKind;
        if (!TryTranslateMouseButton(kind, out var button, out var pressed))
        {
            button = InputTranslation.TranslateMouseButton(point.Properties);
            pressed = false;
        }

        if (button == PlatformMouseButton.None)
        {
            this.DebugInputLog("PointerReleased: could not determine button");
            return;
        }

        this.DebugInputLog($"PointerReleased: forwarding button={button} pressed={pressed}");
        viewModel.EngineService.Input.PushButtonEvent(
            viewId,
            new EditorButtonEventManaged
            {
                button = button,
                pressed = pressed,
                timestamp = DateTime.UtcNow,
                position = pos,
            });
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
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y);
        var delta = this.hasPointerPosition ? (pos - this.lastPointerPosition) : Vector2.Zero;

        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        if (delta != Vector2.Zero)
        {
            this.DebugInputLog($"PointerMoved: pos=({pos.X:0.0},{pos.Y:0.0}) delta=({delta.X:0.0},{delta.Y:0.0})");
        }

        viewModel.EngineService.Input.PushMouseMotion(
            viewId,
            new EditorMouseMotionEventManaged
            {
                motion = delta,
                position = pos,
                timestamp = DateTime.UtcNow,
            });
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

        this.SyncAltKeyStateIfNeeded(viewModel, viewId);

        var point = e.GetCurrentPoint(this.SwapChainPanel);
        var pos = new Vector2((float)point.Position.X, (float)point.Position.Y);
        this.lastPointerPosition = pos;
        this.hasPointerPosition = true;

        var rawDelta = point.Properties.MouseWheelDelta;
        var ticks = rawDelta / 120.0f;
        if (Math.Abs(ticks) <= float.Epsilon)
        {
            this.DebugInputLog("PointerWheelChanged: zero delta");
            this.DebugWheelLog($"rawDelta={rawDelta} ticks={ticks:0.00} (ignored: zero)");
            return;
        }

        this.DebugWheelLog(
            $"rawDelta={rawDelta} ticks={ticks:0.00} pos=({pos.X:0.0},{pos.Y:0.0})");

        this.DebugInputLog($"PointerWheelChanged: forwarding ticks={ticks:0.00}");
        viewModel.EngineService.Input.PushMouseWheel(
            viewId,
            new EditorMouseWheelEventManaged
            {
                scroll = new Vector2(0.0f, (float)ticks),
                position = pos,
                timestamp = DateTime.UtcNow,
            });
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
        var requestedViewModel = viewModel;
        var requestId = Interlocked.Increment(ref this.attachRequestId);

        try
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

            // Use a non-empty tag for surface requests — control `Name` is often
            // the empty string (not null), so the null-coalescing operator
            // doesn't help. Treat empty/whitespace as missing and fall back.
            var requestTag = string.IsNullOrWhiteSpace(this.Name) ? "viewport" : this.Name;
            var request = viewModel.CreateSurfaceRequest(requestTag);
            var lease = await viewModel.EngineService.AttachViewportAsync(request, this.SwapChainPanel, cancellationToken).ConfigureAwait(true);

            // Validate that we still want this lease. If any condition fails we dispose
            // it and clean up. This reduces duplicated checks and paths above.
            var shouldKeepLease = this.IsLoaded && !this.isDisposed && ReferenceEquals(requestedViewModel, this.ViewModel) && requestId == this.attachRequestId;
            if (!shouldKeepLease)
            {
                this.LogAttachOutcomeIgnored(!this.IsLoaded || this.isDisposed ? "ControlStateChanged" : !ReferenceEquals(requestedViewModel, this.ViewModel) ? "ViewModelChanged" : "SupersededRequest");
                await this.DisposeLeaseSilentlyAsync(lease).ConfigureAwait(true);
                if (requestId == this.attachRequestId)
                {
                    await this.CancelPendingAttachAsync().ConfigureAwait(true);
                }

                return;
            }

            this.surfaceLease = lease;
            this.LogSurfaceAttached(viewModel.ViewportId);

            // Create an engine view for this viewport and associate it with the
            // UI-managed view model. The UI owns view lifecycle: create -> destroy.
            try
            {
                // Only create if we don't already have an assigned view id
                if (!viewModel.AssignedViewId.IsValid)
                {
                    // Compute a reasonable initial pixel size for the view
                    _ = this.TryGetSwapChainPixelSize(out var pixelW, out var pixelH);

                    var cfg = new ViewConfigManaged
                    {
                        Name = requestTag,
                        Purpose = "Viewport",
                        CompositingTarget = lease.Key.ViewportId,
                        Width = pixelW,
                        Height = pixelH,
                        ClearColor = viewModel.ClearColor,
                    };

                    var created = await viewModel.EngineService.CreateViewAsync(cfg).ConfigureAwait(true);
                    if (created.IsValid)
                    {
                        viewModel.AssignedViewId = created;
                        this.LogViewCreated(viewModel.ViewportId, created);
                    }
                }
            }
            catch (Exception ex)
            {
                this.LogCreateViewFailed(viewModel.ViewportId, ex);
            }

            // Perform the initial resize unconditionally (do not cancel via the
            // attach token) so the swapchain receives its first backbuffer size.
            // Subsequent resize events are handled by the debounced pipeline and
            // are cancellable.
            await this.NotifyViewportResizeAsync(CancellationToken.None).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            this.LogAttachmentCanceled(reason);
        }
#pragma warning disable CA1031 // Engine attachment failures should be logged and suppressed to keep UI responsive.
        catch (Exception ex)
        {
            this.LogAttachmentFailed(ex);
        }
#pragma warning restore CA1031
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
            // If the UI created an engine view for this viewport, destroy it
            // before we dispose the surface lease. The UI owns view lifecycle.
            var vm = this.ViewModel;
            if (vm?.AssignedViewId.IsValid == true && vm.EngineService != null)
            {
                try
                {
                    await vm.EngineService.DestroyViewAsync(vm.AssignedViewId).ConfigureAwait(true);
                    vm.AssignedViewId = ViewIdManaged.Invalid;
                    this.LogViewDestroyed(vm.ViewportId);
                }
                catch (Exception ex)
                {
                    this.LogDestroyViewFailed(vm.ViewportId, ex);
                }
            }

            await this.surfaceLease.DisposeAsync().ConfigureAwait(true);
            this.LogLeaseDisposed(GetViewportId(this.ViewModel));
        }
#pragma warning disable CA1031 // Disposal errors are logged but should not crash the control during teardown.
        catch (Exception ex)
        {
            this.LogLeaseDisposeFailed(ex);
        }
#pragma warning restore CA1031
        finally
        {
            this.surfaceLease = null;
        }
    }

    private void UnregisterSwapChainPanelSizeChanged()
    {
        if (!this.swapChainSizeHooked)
        {
            return;
        }

        if (this.SwapChainPanel != null)
        {
            this.SwapChainPanel.SizeChanged -= this.OnSwapChainPanelSizeChanged;
        }

        // Stop and dispose the Rx subscription and subject when we detach so
        // we stop dispatching debounced resize calls.
        try
        {
            this.sizeChangedSubscription?.Dispose();
        }
        catch
        {
        }

        this.sizeChangedSubscription = null;

        try
        {
            this.sizeChangedSubject?.OnCompleted();
            this.sizeChangedSubject?.Dispose();
        }
        catch
        {
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
        catch (Exception ex)
        {
            this.LogLeaseDisposeFailed(ex);
        }
    }
}
