// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Runtime.InteropServices;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// A control that displays a 3D viewport with overlay controls.
/// </summary>
public sealed partial class Viewport : UserControl, IAsyncDisposable // TODO: xaml.cs is doing too much instead of the view model
{
    private const string LoggerCategoryName = "Oxygen.Editor.WorldEditor.Controls.Viewport";
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

    /// <summary>
    /// Initializes a new instance of the <see cref="Viewport"/> class.
    /// </summary>
    public Viewport()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;

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

        await this.DetachSurfaceAsync("ActiveLoadCountZero").ConfigureAwait(true);
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
