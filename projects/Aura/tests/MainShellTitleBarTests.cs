// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Resources;
using DroidNet.Routing;
using DroidNet.Tests;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class MainShellTitleBarTests : VisualUserInterfaceTests
{
    public required TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task TitleBar_PreservesAuraLayoutAsync(bool withDocumentTabs) => EnqueueAsync(async () =>
    {
        var (shell, viewModel) = await this.LoadShellAsync(withDocumentTabs).ConfigureAwait(true);
        using (viewModel)
        {
            var grid = (Grid)shell.FindName("CustomTitleBar");
            var icon = (FrameworkElement)shell.FindName("AppIcon");
            var primary = (StackPanel)shell.FindName("PrimaryCommands");
            var host = (TitleBar)shell.FindName("TitleBarHost");
            var surface = (FrameworkElement)shell.FindName("TitleBarDragSurface");

            _ = host.AutoRefreshDragRegions.Should().BeTrue();
            _ = grid.RowDefinitions[0].ActualHeight.Should().Be(32);
            _ = grid.RowDefinitions[1].ActualHeight.Should().Be(withDocumentTabs ? 32 : 0);
            _ = host.ActualHeight.Should().Be(withDocumentTabs ? 64 : 32);
            _ = surface.ActualHeight.Should().Be(32);
            _ = primary.Spacing.Should().Be(12);
            _ = grid.ColumnDefinitions[2].MinWidth.Should().Be(48);
            _ = icon.Margin.Should().Be(withDocumentTabs ? new Thickness(8, 8, 12, 8) : new Thickness(4, 4, 8, 4));
            _ = grid.ColumnDefinitions[4].ActualWidth.Should().BeApproximately(
                VisualUserInterfaceTestsApp.MainWindow.AppWindow.TitleBar.RightInset / shell.XamlRoot.RasterizationScale,
                0.5);

            await UnloadTestContentAsync(shell).ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task TitleBar_AutomaticallyTracksInteractiveBoundsAsync() => EnqueueAsync(async () =>
    {
        var (shell, viewModel) = await this.LoadShellAsync(withDocumentTabs: true).ConfigureAwait(true);
        using (viewModel)
        {
            var secondary = (StackPanel)shell.FindName("SecondaryCommands");
            var tabs = (FrameworkElement)shell.FindName("DocumentTabStrip");
            var icon = (FrameworkElement)shell.FindName("AppIcon");
            _ = TitleBar.GetIsDragRegion(secondary).Should().BeFalse();
            _ = TitleBar.GetIsDragRegion(tabs).Should().BeFalse();
            _ = TitleBar.GetIsDragRegion(icon).Should().BeTrue();

            AssertInteractiveBounds(shell, secondary);
            AssertInteractiveBounds(shell, tabs);
            secondary.Width = secondary.ActualWidth + 24;
            await WaitForLayoutAsync(shell).ConfigureAwait(true);
            AssertInteractiveBounds(shell, secondary);

            secondary.Visibility = Visibility.Collapsed;
            await WaitForLayoutAsync(shell).ConfigureAwait(true);
            var regions = GetPassthroughRegions();
            _ = regions.Length.Should().Be(1, "only the document tab strip remains interactive");
            AssertInteractiveBounds(shell, tabs);

            await UnloadTestContentAsync(shell).ConfigureAwait(true);
            _ = GetPassthroughRegions().Should().BeNullOrEmpty();

            await LoadTestContentAsync(shell).ConfigureAwait(true);
            await WaitForLayoutAsync(shell).ConfigureAwait(true);
            AssertInteractiveBounds(shell, tabs);
            await UnloadTestContentAsync(shell).ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task TitleBar_WindowClosedBeforeUnload_DoesNotRaiseUnhandledExceptionAsync() => EnqueueAsync(async () =>
    {
        var window = new Window { ExtendsContentIntoTitleBar = true };
        using var viewModel = await this.CreateViewModelAsync(window, withDocumentTabs: true).ConfigureAwait(true);
        var shell = new MainShellView { ViewModel = viewModel };
        var loaded = new TaskCompletionSource();
        var unloaded = new TaskCompletionSource();
        var exceptions = new List<Exception>();
        shell.Loaded += (_, _) => loaded.TrySetResult();
        shell.Unloaded += (_, _) => unloaded.TrySetResult();
        Application.Current.UnhandledException += OnUnhandledException;
        var closed = false;
        try
        {
            window.Content = shell;
            window.Activate();
            await loaded.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            await WaitForLayoutAsync(shell).ConfigureAwait(true);
            window.Close();
            closed = true;
            await unloaded.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            _ = exceptions.Should().BeEmpty();
        }
        finally
        {
            if (!closed)
            {
                window.Close();
            }

            Application.Current.UnhandledException -= OnUnhandledException;
        }

        void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs args)
        {
            _ = sender;
            exceptions.Add(args.Exception);
            args.Handled = true;
        }
    });

    private static Windows.Graphics.RectInt32[] GetPassthroughRegions()
        => InputNonClientPointerSource.GetForWindowId(VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id)
            .GetRegionRects(NonClientRegionKind.Passthrough);

    private static void AssertInteractiveBounds(MainShellView shell, FrameworkElement element)
    {
        var bounds = element.TransformToVisual(null).TransformBounds(new Rect(0, 0, element.ActualWidth, element.ActualHeight));
        var scale = shell.XamlRoot.RasterizationScale;
        _ = GetPassthroughRegions().Should().Contain(region =>
            region.X == (int)(bounds.X * scale)
            && region.Y == (int)(bounds.Y * scale)
            && region.Width == (int)(bounds.Width * scale)
            && region.Height == (int)(bounds.Height * scale));
    }

    private static async Task WaitForLayoutAsync(MainShellView shell)
    {
        shell.UpdateLayout();
        await WaitForRenderAsync().ConfigureAwait(true);
        await Task.Delay(50).ConfigureAwait(true);
        shell.UpdateLayout();
        await WaitForRenderAsync().ConfigureAwait(true);
    }

    private async Task<(MainShellView Shell, MainShellViewModel ViewModel)> LoadShellAsync(bool withDocumentTabs)
    {
        var window = VisualUserInterfaceTestsApp.MainWindow;
        window.ExtendsContentIntoTitleBar = true;
        var viewModel = await this.CreateViewModelAsync(window, withDocumentTabs).ConfigureAwait(true);
        var shell = new MainShellView { ViewModel = viewModel };
        await LoadTestContentAsync(shell).ConfigureAwait(true);
        await WaitForLayoutAsync(shell).ConfigureAwait(true);
        return (shell, viewModel);
    }

    private async Task<MainShellViewModel> CreateViewModelAsync(Window window, bool withDocumentTabs)
    {
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };
        var settings = new Mock<ISettingsService<IAppearanceSettings>>();
        _ = settings.SetupGet(service => service.Settings).Returns(Mock.Of<IAppearanceSettings>());
        var managed = new Mock<IManagedWindow>();
        _ = managed.SetupGet(context => context.Id).Returns(window.AppWindow.Id);
        _ = managed.SetupGet(context => context.Window).Returns(window);
        _ = managed.SetupProperty(context => context.Decorations, new WindowDecorationOptions
        {
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default with { WithDocumentTabs = withDocumentTabs },
        });
        var manager = new Mock<IWindowManagerService>();
        _ = manager.SetupGet(service => service.OpenWindows).Returns([managed.Object]);
        var viewModel = new MainShellViewModel(hosting, new AssetResolverService(this.LoggerFactory), settings.Object, manager.Object);
        var navigation = new Mock<INavigationContext>();
        _ = navigation.SetupGet(context => context.NavigationTarget).Returns(window);
        await viewModel.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), navigation.Object).ConfigureAwait(true);
        return viewModel;
    }
}
