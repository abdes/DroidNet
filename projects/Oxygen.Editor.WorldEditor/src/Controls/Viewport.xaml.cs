// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.Engine;

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
    private int activeLoadCount;
    private CancellationTokenSource? attachCancellationSource;
    private ILogger logger = NullLoggerFactory.Instance.CreateLogger(LoggerCategoryName);
    private ViewportViewModel? currentViewModel;
    private bool isDisposed;
    private long attachRequestId;

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
            this.OnViewModelChanged(null, existingViewModel);
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

    private static string GetViewportId(ViewportViewModel? viewModel)
        => viewModel?.ViewportId.ToString("D", CultureInfo.InvariantCulture) ?? "none";

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
        _ = this.NotifyViewportResizeAsync();
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
            var requestTag = this.Name ?? "viewport";
            var request = viewModel.CreateSurfaceRequest(requestTag);
            var lease = await viewModel.EngineService.AttachViewportAsync(request, this.SwapChainPanel, cancellationToken).ConfigureAwait(true);

            if (!this.IsLoaded || this.isDisposed)
            {
                this.LogAttachOutcomeIgnored("ControlStateChanged");
                await this.DisposeLeaseSilentlyAsync(lease).ConfigureAwait(true);
                if (requestId == this.attachRequestId)
                {
                    await this.CancelPendingAttachAsync().ConfigureAwait(true);
                }
                return;
            }

            if (!ReferenceEquals(requestedViewModel, this.ViewModel))
            {
                this.LogAttachOutcomeIgnored("ViewModelChanged");
                await this.DisposeLeaseSilentlyAsync(lease).ConfigureAwait(true);
                if (requestId == this.attachRequestId)
                {
                    await this.CancelPendingAttachAsync().ConfigureAwait(true);
                }
                return;
            }

            if (requestId != this.attachRequestId)
            {
                this.LogAttachOutcomeIgnored("SupersededRequest");
                await this.DisposeLeaseSilentlyAsync(lease).ConfigureAwait(true);
                return;
            }

            this.surfaceLease = lease;
            this.LogSurfaceAttached(viewModel.ViewportId);
            await this.NotifyViewportResizeAsync(cancellationToken).ConfigureAwait(true);
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
        await this.DetachSurfaceAsync("Dispose").ConfigureAwait(true);
        await this.CancelPendingAttachAsync().ConfigureAwait(true);

        this.currentViewModel = null;
        GC.SuppressFinalize(this);
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
