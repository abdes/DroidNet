// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

[TestClass]
public sealed partial class ExplorerRoutingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Switching tiles/list repeatedly replaces disposed layouts while retaining selection/query and one catalog initialization.</summary>
    /// <returns>The actual-router regression for the reported disposed-layout crash.</returns>
    [TestMethod]
    public Task ActualExplorerRouterNeverReusesDisposedLayouts() => EnqueueAsync(async () =>
    {
        var asset = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([asset]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentExplorerState();
        var container = new Container();
        await using var containerLifetime = container.ConfigureAwait(true);
        container.RegisterInstance<IProjectContextService>(projects);
        container.RegisterInstance(provider.Object);
        container.RegisterInstance(state);
        container.RegisterInstance(CreateStatusHosting());
        container.RegisterInstance<ILoggerFactory>(NullLoggerFactory.Instance);
        ContentExplorerViewModel.RegisterAssetLayouts(container);
        using var outlet = new ExplorerLayoutOutlet();
        var routes = new Routes([new Route { Path = string.Empty, Children = new Routes([new Route { Outlet = "right", Path = "assets/tiles", ViewModelType = typeof(TilesLayoutViewModel) }, new Route { Outlet = "right", Path = "assets/list", ViewModelType = typeof(ListLayoutViewModel) },]), },]);
        _ = container.WithLocalRouting(routes, new LocalRouterContext(new object()) { RootViewModel = outlet, ParentRouter = Mock.Of<IRouter>() });
        var router = container.Resolve<IRouter>();
        await router.NavigateAsync("/(right:assets/tiles)").ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var first = outlet.Layout.Should().BeOfType<TilesLayoutViewModel>().Subject;
        first.SelectedAsset = asset;
        state.Query.SearchText = "Blue";
        var previous = (AssetsLayoutViewModel)first;
        for (var index = 0; index < 4; index++)
        {
            var tiles = index % 2 != 0;
            await router.NavigateAsync(tiles ? "/(right:assets/tiles)" : "/(right:assets/list)").ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            var current = outlet.Layout!;
            _ = current.Should().NotBeSameAs(previous).And.NotBeSameAs(first);
            _ = current.SelectedAsset!.Identity.Should().Be(asset.Identity);
            _ = current.Query.SearchText.Should().Be("Blue");
            UserControl view = tiles ? container.Resolve<TilesLayoutView>() : container.Resolve<ListLayoutView>();
            ((DroidNet.Mvvm.IViewFor)view).ViewModel = current;
            await LoadTestContentAsync(view).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            previous = current;
        }

        var disposed = () => first.OnNavigatedToAsync(null!, null!);
        _ = await disposed.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    private sealed partial class ExplorerLayoutOutlet : AbstractOutletContainer
    {
        public ExplorerLayoutOutlet() => this.Outlets.Add("right", (nameof(this.Layout), null));

        public AssetsLayoutViewModel? Layout => this.Outlets["right"].viewModel as AssetsLayoutViewModel;
    }
}
