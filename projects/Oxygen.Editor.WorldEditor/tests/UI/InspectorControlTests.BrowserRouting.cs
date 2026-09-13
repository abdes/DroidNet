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
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises layout replacement through the production local router and outlet disposal contract.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Switching tiles/list repeatedly replaces disposed layouts while retaining selection/query and one catalog initialization.</summary>
    /// <returns>The actual-router regression for the reported disposed-layout crash.</returns>
    [TestMethod]
    public Task ActualBrowserRouterNeverReusesDisposedLayouts() => EnqueueAsync(async () =>
    {
        var asset = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([asset]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var container = new Container();
        await using var containerLifetime = container.ConfigureAwait(true);
        container.RegisterInstance<IProjectContextService>(projects);
        container.RegisterInstance(provider.Object);
        container.RegisterInstance(state);
        container.RegisterInstance(CreateStatusHosting());
        container.RegisterInstance<IBuiltinCatalogDiscovery>(new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        container.RegisterInstance<ILoggerFactory>(NullLoggerFactory.Instance);
        ContentBrowserViewModel.RegisterAssetLayouts(container);
        using var outlet = new BrowserLayoutOutlet();
        var routes = new Routes(
        [
            new Route
            {
                Path = string.Empty,
                Children = new Routes(
                [
                    new Route { Outlet = "right", Path = "assets/tiles", ViewModelType = typeof(TilesLayoutViewModel) },
                    new Route { Outlet = "right", Path = "assets/list", ViewModelType = typeof(ListLayoutViewModel) },
                ]),
            },
        ]);
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
            _ = current.SelectedAsset!.IdentityUri.Should().Be(asset.IdentityUri);
            _ = current.Query.SearchText.Should().Be("Blue");
            UserControl view = tiles ? container.Resolve<TilesLayoutView>() : container.Resolve<ListLayoutView>();
            ((DroidNet.Mvvm.IViewFor)view).ViewModel = current;
            await LoadTestContentAsync(view).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            previous = current;
        }

        var disposed = () => first.OnNavigatedToAsync(null!, null!);
        _ = await disposed.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
    });

    private sealed partial class BrowserLayoutOutlet : AbstractOutletContainer
    {
        public BrowserLayoutOutlet() => this.Outlets.Add("right", (nameof(this.Layout), null));

        public AssetsLayoutViewModel? Layout => this.Outlets["right"].viewModel as AssetsLayoutViewModel;
    }
}
