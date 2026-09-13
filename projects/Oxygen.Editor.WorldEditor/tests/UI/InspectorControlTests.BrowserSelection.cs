// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Shell;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks selection through real list/tile replacement and live catalog updates.</summary>
public sealed partial class InspectorControlTests
{
    private static readonly string[] BrowserSelectionQueries = ["Bl", "Cube", "Blu", "absent"];

    /// <summary>Changing layouts retains the current asset instead of restoring each layout's old selection.</summary>
    /// <returns>The asynchronous rendered selection journey.</returns>
    [TestMethod]
    public Task BrowserSelectionFollowsListTileSwitchAndPublication() => EnqueueAsync(async () =>
    {
        var blue = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
        var red = CreateNavigationAsset("/Content/Materials/Red.omat.json", AssetKind.Material);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([blue, red]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using var list = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        using var tiles = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var listView = new ListLayoutView { ViewModel = list };
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var listSelector = listView.FindDescendant<ListViewBase>()!;
        listSelector.SelectedIndex = 0;
        _ = list.SelectedAsset!.IdentityUri.Should().Be(blue.IdentityUri);

        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var tileView = new TilesLayoutView { ViewModel = tiles };
        await LoadTestContentAsync(tileView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var tileSelector = tileView.FindDescendant<ListViewBase>()!;
        _ = tileSelector.SelectedItem.Should().BeOfType<AssetBrowserRow>().Which.Item.IdentityUri.Should().Be(blue.IdentityUri);
        listSelector.SelectedItem = null;
        tileSelector.SelectedIndex = 1;
        _ = tiles.SelectedAsset!.IdentityUri.Should().Be(red.IdentityUri);

        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = listSelector.SelectedItem.Should().BeOfType<AssetBrowserRow>().Which.Item.IdentityUri.Should().Be(red.IdentityUri);
        var selectedRow = listSelector.SelectedItem;
        var selectedContainer = (Control)listSelector.ContainerFromItem(selectedRow);
        _ = selectedContainer.Focus(FocusState.Keyboard).Should().BeTrue();
        var focused = FocusManager.GetFocusedElement(listView.XamlRoot);
        var published = red with { RuntimeAvailability = AssetRuntimeAvailability.Mounted, CookedUri = new("asset:///Content/Materials/Red.omat") };
        updates.OnNext([published, blue]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = listSelector.SelectedItem.Should().BeSameAs(selectedRow);
        _ = list.SelectedAsset.Should().Be(published);
        _ = FocusManager.GetFocusedElement(listView.XamlRoot).Should().BeSameAs(focused);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>Rapid filters and folder navigation cannot restore a hidden command target from an inactive layout.</summary>
    /// <returns>The asynchronous rendered query/selection regression.</returns>
    [TestMethod]
    public Task HiddenBrowserSelectionDoesNotReturnAfterViewSwitch() => EnqueueAsync(async () =>
    {
        var blue = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
        var cube = CreateNavigationAsset("/Content/Geometry/Cube.ogeo.json", AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([blue, cube]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using var list = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        using var tiles = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var listView = new ListLayoutView { ViewModel = list };
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        listView.FindDescendant<ListViewBase>()!.SelectedIndex = 0;
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var tileView = new TilesLayoutView { ViewModel = tiles };
        await LoadTestContentAsync(tileView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var invoked = false;
        list.ItemInvoked += (_, _) => invoked = true;
        tiles.ItemInvoked += (_, _) => invoked = true;
        list.InvokeItemCommand.Execute(blue);
        _ = invoked.Should().BeFalse("unloaded layouts cannot invoke their old selection");
        foreach (var text in BrowserSelectionQueries)
        {
            state.Query.SearchText = text;
        }

        _ = tiles.SelectedAsset.Should().BeNull();
        _ = tileView.FindDescendant<ListViewBase>()!.SelectedItem.Should().BeNull();
        tiles.InvokeItemCommand.Execute(blue);
        _ = invoked.Should().BeFalse();
        state.Query.ClearAllCommand.Execute(parameter: null);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = list.SelectedAsset.Should().BeNull();
        _ = listView.FindDescendant<ListViewBase>()!.SelectedItem.Should().BeNull();
        list.SelectedAsset = blue;
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)?selected=%2FContent%2FGeometry");
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)?selected=.");
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        _ = tiles.SelectedAsset.Should().BeNull();
    });
}
