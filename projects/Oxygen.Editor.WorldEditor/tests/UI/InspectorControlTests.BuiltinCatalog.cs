// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises engine-owned choices and offline catalog notices in the actual editor views.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>All eleven native names remain authorable offline, with alias text and a working retry.</summary>
    /// <returns>The asynchronous picker-availability regression.</returns>
    [TestMethod]
    public Task BuiltinPickerShowsAllNativeNamesAndRecoversItsCatalog() => EnqueueAsync(async () =>
    {
        var discovery = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var live = discovery.Snapshot;
        discovery.SetSnapshot(new(live.Catalog, IsLastKnown: true, "Using the last-known engine catalog. Preview unavailable."));
        var catalog = new Mock<Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider>();
        _ = catalog.SetupGet(value => value.Items).Returns(Observable.Empty<IReadOnlyList<Oxygen.Editor.ContentBrowser.AssetIdentity.ContentBrowserAssetItem>>());
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<Oxygen.Editor.ContentBrowser.AssetIdentity.AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var materials = new Mock<IMaterialPickerService>();
        _ = materials.SetupGet(value => value.Results).Returns(Observable.Return<IReadOnlyList<MaterialPickerResult>>([]));
        _ = materials.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = new GeometryViewModel(CreateStatusHosting(), catalog.Object, materials.Object, discovery, Mock.Of<Services.ISceneContentDemandService>()) { IsExpanded = true };
        var view = new GeometryView { ViewModel = model, Width = 440 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var engine = model.Groups.Single(group => string.Equals(group.Key, "Engine", StringComparison.Ordinal));
        _ = engine.Items.Should().Equal(live.Catalog!.Geometries, (item, definition) => item.Item.Uri == definition.AssetUri);
        _ = engine.Items.Should().HaveCount(11).And.OnlyContain(item => item.Item.IsEnabled);
        _ = engine.Items.Single(item => string.Equals(item.Item.Name, "GeodesicSphere", StringComparison.Ordinal)).Item.DisplayType.Should().Contain("Alias of IcoSphere");
        var owner = (SplitButton)view.FindName("AssetSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var notice = ((FrameworkElement)flyout.Content).FindDescendant<InfoBar>()!;
            _ = notice.IsOpen.Should().BeTrue();
            _ = notice.Message.Should().Contain("Preview unavailable");
            discovery.RefreshResult = live;
            await model.RetryBuiltinCatalogCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = notice.IsOpen.Should().BeFalse();
            _ = engine.Items.Should().HaveCount(11).And.OnlyContain(item => !item.Item.DisplayType.Contains("Preview unavailable", StringComparison.Ordinal));
        }
        finally
        {
            flyout.Hide();
        }
    });

    /// <summary>The catalog notice stays scoped to engine browsing and clears when retry succeeds.</summary>
    /// <returns>The asynchronous browser-scope regression.</returns>
    [TestMethod]
    public Task BuiltinCatalogNoticeFollowsBrowserScopeAndRetry() => EnqueueAsync(async () =>
    {
        var discovery = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var live = discovery.Snapshot;
        discovery.SetSnapshot(new(Catalog: null, IsLastKnown: false, "Engine catalog unavailable. No last-known catalog is available."));
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<ContentBrowserAssetItem>>([]));
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var projects = new ProjectContextService();
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Content/Materials"]);
        using var model = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), discovery);
        var view = new ListLayoutView { ViewModel = model, Width = 500, Height = 300 };
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var notice = view.FindDescendant<InfoBar>()!;
        _ = notice.IsOpen.Should().BeFalse();
        state.SetSelectedFolders(["/Engine"]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = notice.IsOpen.Should().BeTrue();
        _ = notice.Message.Should().Contain("No last-known catalog");
        discovery.RefreshResult = live;
        await model.RetryBuiltinCatalogCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = notice.IsOpen.Should().BeFalse();
    });
}
