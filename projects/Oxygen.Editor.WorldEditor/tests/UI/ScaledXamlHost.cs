// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Hosting;
using Windows.Graphics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Hosts real XAML at an explicit rasterization scale without changing monitor settings.</summary>
internal sealed partial class ScaledXamlHost : IDisposable
{
    private readonly DesktopWindowXamlSource source = new();
    private readonly SizeInt32 originalSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;

    /// <summary>Verifies that all expanded content remains reachable through its outer scroller.</summary>
    /// <param name="scroller">The scrolling surface.</param>
    /// <returns>The task completing after the final offset is rendered.</returns>
    public static async Task ScrollToEndAsync(Microsoft.UI.Xaml.Controls.ScrollViewer scroller)
    {
        _ = scroller.ChangeView(horizontalOffset: null, scroller.ScrollableHeight, zoomFactor: null, disableAnimation: true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = scroller.VerticalOffset.Should().BeApproximately(scroller.ScrollableHeight, 1);
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
}
