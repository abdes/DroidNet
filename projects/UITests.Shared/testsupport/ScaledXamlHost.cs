// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Hosting;
using Windows.Graphics;

namespace DroidNet.Tests;

/// <summary>Hosts real XAML at an explicit rasterization scale without changing monitor settings.</summary>
internal sealed partial class ScaledXamlHost : IDisposable
{
    private readonly DesktopWindowXamlSource source = new();
    private readonly SizeInt32 originalSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;

    /// <summary>Verifies that all expanded content remains reachable through its outer scroller.</summary>
    /// <param name="scroller">The scrolling surface.</param>
    /// <param name="cancellationToken">Cancels the test operation.</param>
    /// <returns>The task completing after the final offset is rendered.</returns>
    public static async Task ScrollToEndAsync(ScrollViewer scroller, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        await SettleLayoutAsync(scroller, timeout.Token).ConfigureAwait(true);
        while (true)
        {
            timeout.Token.ThrowIfCancellationRequested();
            var requestedOffset = scroller.ScrollableHeight;
            var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            void OnViewChanged(object? sender, ScrollViewerViewChangedEventArgs args)
            {
                if (!args.IsIntermediate && (Math.Abs(scroller.VerticalOffset - requestedOffset) <= 1
                    || Math.Abs(scroller.VerticalOffset - scroller.ScrollableHeight) <= 1))
                {
                    _ = completed.TrySetResult();
                }
            }

            scroller.ViewChanged += OnViewChanged;
            try
            {
                var accepted = scroller.ChangeView(horizontalOffset: null, requestedOffset, zoomFactor: null, disableAnimation: true);
                if (Math.Abs(scroller.VerticalOffset - requestedOffset) <= 1)
                {
                    _ = completed.TrySetResult();
                }
                else
                {
                    _ = accepted.Should().BeTrue("the loaded scroller must accept its end offset");
                }

                await completed.Task.WaitAsync(timeout.Token).ConfigureAwait(true);
            }
            finally
            {
                scroller.ViewChanged -= OnViewChanged;
            }

            await SettleLayoutAsync(scroller, timeout.Token).ConfigureAwait(true);
            if (Math.Abs(scroller.ScrollableHeight - requestedOffset) <= 1)
            {
                _ = scroller.VerticalOffset.Should().BeApproximately(scroller.ScrollableHeight, 1);
                return;
            }
        }
    }

    /// <summary>Loads the content in a child island and verifies its effective scale.</summary>
    /// <param name="content">The finite-sized control tree to realize.</param>
    /// <param name="scale">The requested rasterization scale.</param>
    /// <param name="cancellationToken">The test lifetime.</param>
    /// <returns>The task completing after layout and rendering.</returns>
    public async Task LoadAsync(FrameworkElement content, double scale, CancellationToken cancellationToken)
    {
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var width = checked((int)Math.Ceiling(content.Width * scale));
        var height = checked((int)Math.Ceiling(content.Height * scale));
        window.Resize(new(width + 60, height + 100));
        this.source.Initialize(window.Id);
        this.source.SiteBridge.OverrideScale = (float)scale;
        this.source.SiteBridge.MoveAndResize(new(0, 0, width, height));
        var loaded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        content.Loaded += OnLoaded;
        try
        {
            this.source.Content = content;
            this.source.SiteBridge.Show();
            this.source.SiteBridge.MoveInZOrderAtTop();
            await loaded.Task.WaitAsync(TimeSpan.FromSeconds(10), cancellationToken).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(content.UpdateLayout).ConfigureAwait(true);
            _ = content.XamlRoot.RasterizationScale.Should().BeApproximately(scale, 0.001);
            _ = content.ActualWidth.Should().BeApproximately(content.Width, 1);
            _ = content.ActualHeight.Should().BeApproximately(content.Height, 1);
        }
        finally
        {
            content.Loaded -= OnLoaded;
        }

        void OnLoaded(object sender, RoutedEventArgs args) => loaded.TrySetResult();
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.source.Dispose();
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(this.originalSize);
    }

    private static async Task SettleLayoutAsync(ScrollViewer scroller, CancellationToken cancellationToken)
    {
        // Let initialization work (including reading-position restoration) finish
        // before requesting a scroll. EnqueueAsync may execute inline on this thread.
        var idle = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = scroller.DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () => idle.TrySetResult())
            .Should().BeTrue("the test dispatcher must still be running");
        await idle.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(scroller.UpdateLayout)
            .WaitAsync(cancellationToken).ConfigureAwait(true);
    }
}
