// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.Engine;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// A control that displays a 3D viewport with overlay controls.
/// </summary>
public sealed partial class Viewport : UserControl // TODO: xaml.cs is doing too much instead of the view model
{
    private IViewportSurfaceLease? surfaceLease;
    private bool swapChainSizeHooked;
    private int activeLoadCount;
    private CancellationTokenSource? attachCancellationSource;

    /// <summary>
    /// Initializes a new instance of the <see cref="Viewport"/> class.
    /// </summary>
    public Viewport()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    /// <summary>
    /// Gets or sets the ViewModel for this viewport.
    /// </summary>
    public ViewportViewModel? ViewModel
    {
        get => this.DataContext as ViewportViewModel;
        set => this.DataContext = value;
    }

    private void OnLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        this.Log($"OnLoaded invoked. current load count={this.activeLoadCount}");
        this.activeLoadCount++;

        if (this.surfaceLease != null)
        {
            this.Log("Existing engine surface detected on load; issuing resize only.");
            _ = this.NotifyViewportResizeAsync();
            return;
        }

        if (!this.swapChainSizeHooked)
        {
            this.SwapChainPanel.SizeChanged += this.OnSwapChainPanelSizeChanged;
            this.swapChainSizeHooked = true;
            this.Log("Registered SwapChainPanel size hook.");
        }

        this.Log("Attaching viewport surface from OnLoaded.");
        _ = this.AttachSurfaceAsync();
    }

    private void OnSwapChainPanelSizeChanged(object sender, Microsoft.UI.Xaml.SizeChangedEventArgs e)
    {
        this.Log($"SwapChainPanel size changed to {e.NewSize.Width}x{e.NewSize.Height}.");
        _ = this.NotifyViewportResizeAsync();
    }

    private async Task NotifyViewportResizeAsync(CancellationToken cancellationToken = default)
    {
        if (this.surfaceLease == null)
        {
            return;
        }

        if (this.SwapChainPanel == null || this.XamlRoot == null)
        {
            return;
        }

        var scale = this.XamlRoot.RasterizationScale;
        var pixelWidth = (uint)Math.Max(1, Math.Round(this.SwapChainPanel.ActualWidth * scale));
        var pixelHeight = (uint)Math.Max(1, Math.Round(this.SwapChainPanel.ActualHeight * scale));

        if (pixelWidth == 0 || pixelHeight == 0)
        {
            return;
        }

        try
        {
            await this.surfaceLease.ResizeAsync(pixelWidth, pixelHeight, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            /* cancellation is expected when detaching */
        }
        catch (Exception ex)
        {
            this.Log($"Failed to resize viewport surface: {ex}");
        }
    }

    private async void OnUnloaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        this.Log($"OnUnloaded invoked. current load count={this.activeLoadCount}");
        if (this.activeLoadCount > 0)
        {
            this.activeLoadCount--;
            this.Log($"Decremented load count to {this.activeLoadCount}.");
        }

        if (this.activeLoadCount > 0)
        {
            this.Log("Unload ignored because other load references remain.");
            return;
        }

        if (this.swapChainSizeHooked)
        {
            this.SwapChainPanel.SizeChanged -= this.OnSwapChainPanelSizeChanged;
            this.swapChainSizeHooked = false;
            this.Log("Unregistered SwapChainPanel size hook.");
        }

        if (this.surfaceLease != null)
        {
            this.Log("Detaching viewport surface because load count reached zero.");
            await this.DetachSurfaceAsync().ConfigureAwait(true);
        }
    }

    private async Task AttachSurfaceAsync()
    {
        if (this.ViewModel?.EngineService == null)
        {
            this.Log("Viewport ViewModel does not expose an engine service; attachment skipped.");
            return;
        }

        if (this.SwapChainPanel == null)
        {
            this.Log("SwapChainPanel not ready; attachment skipped.");
            return;
        }

        this.attachCancellationSource?.Cancel();
        this.attachCancellationSource?.Dispose();
        this.attachCancellationSource = new CancellationTokenSource();
        var cancellationToken = this.attachCancellationSource.Token;

        try
        {
            var requestTag = this.Name ?? "viewport";
            var request = this.ViewModel.CreateSurfaceRequest(requestTag);
            this.surfaceLease = await this.ViewModel.EngineService.AttachViewportAsync(request, this.SwapChainPanel, cancellationToken).ConfigureAwait(true);
            this.Log("Viewport surface attached to shared engine.");
            await this.NotifyViewportResizeAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            this.Log("Viewport surface attachment canceled.");
        }
        catch (Exception ex)
        {
            this.Log($"Failed to attach viewport surface: {ex}");
        }
    }

    private async Task DetachSurfaceAsync()
    {
        this.attachCancellationSource?.Cancel();
        this.attachCancellationSource?.Dispose();
        this.attachCancellationSource = null;

        if (this.surfaceLease == null)
        {
            return;
        }

        try
        {
            await this.surfaceLease.DisposeAsync().ConfigureAwait(true);
            this.Log("Viewport surface lease disposed.");
        }
        catch (Exception ex)
        {
            this.Log($"Failed to dispose viewport surface lease: {ex}");
        }
        finally
        {
            this.surfaceLease = null;
        }
    }

    private void Log(string message)
    {
        var id = this.GetHashCode().ToString("X8", CultureInfo.InvariantCulture);
        System.Diagnostics.Debug.WriteLine($"[Viewport:{id}] {message}");
    }
}
