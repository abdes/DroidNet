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

    /// <summary>An explicit reveal survives a hidden view and reaches a distant row without stealing a later focus change.</summary>
    /// <param name="tiles">Whether to exercise tiles.</param>
    /// <returns>The asynchronous rendered reveal regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ExplicitBrowserRevealWaitsForViewAndPreservesNewerFocus(bool tiles) => EnqueueAsync(async () =>
    {
        var items = Enumerable.Range(0, 1000).Select(index => CreateNavigationAsset(
            string.Create(System.Globalization.CultureInfo.InvariantCulture, $"/Content/Materials/M{index}.omat.json"), AssetKind.Material)).ToArray();
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>(items);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins)
            : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        model.SelectedAsset = items[^1];
        model.RevealSelection();
        UserControl view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)model } : new ListLayoutView { ViewModel = (ListLayoutViewModel)model };
        var root = new Grid { Width = 440, Height = 380 };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition());
        var search = new TextBox();
        root.Children.Add(search);
        Grid.SetRow(view, 1);
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var selector = view.FindDescendant<ListViewBase>()!;
        _ = selector.SelectedItem.Should().BeSameAs(model.SelectedRow);
        _ = selector.ContainerFromItem(model.SelectedRow).Should().NotBeNull();
        _ = selector.FindDescendant<ScrollViewer>()!.VerticalOffset.Should().BePositive();
        model.SelectedAsset = items[0];
        model.RevealSelection();
        _ = search.Focus(FocusState.Programmatic).Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = FocusManager.GetFocusedElement(root.XamlRoot).Should().BeSameAs(search);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
    });

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
