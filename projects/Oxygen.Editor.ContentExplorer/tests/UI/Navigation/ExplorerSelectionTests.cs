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
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerSelectionSupport;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

[TestClass]
public sealed partial class ExplorerSelectionTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>An explicit reveal survives a hidden view and reaches a distant row without stealing a later focus change.</summary>
    /// <param name="tiles">Whether to exercise tiles.</param>
    /// <returns>The asynchronous rendered reveal regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ExplicitExplorerRevealWaitsForViewAndPreservesNewerFocus(bool tiles) => EnqueueAsync(async () =>
    {
        var items = Enumerable.Range(0, 1000).Select(index => CreateNavigationAsset(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"/Content/Materials/M{index}.omat"), AssetKind.Material)).ToArray();
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>(items);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting()) : new ListLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        // The first snapshot arrives after navigation; with no view loaded yet there is no frame to wait for.
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await model.InitialLoad.ConfigureAwait(true);
        model.SelectedAsset = items[^1];
        model.RevealSelection();
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)model,
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)model,
        };
        var root = new Grid
        {
            Width = 440,
            Height = 380,
        };
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
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>Changing layouts retains the current asset instead of restoring each layout's old selection.</summary>
    /// <returns>The asynchronous rendered selection journey.</returns>
    [TestMethod]
    public Task ExplorerSelectionFollowsListTileSwitchAndPublication() => EnqueueAsync(async () =>
    {
        var blue = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        var red = CreateNavigationAsset("/Content/Materials/Red.omat", AssetKind.Material);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([blue, red]);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        using var list = new ListLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        using var tiles = new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await list.InitialLoad.ConfigureAwait(true);
        var listView = new ListLayoutView
        {
            ViewModel = list,
        };
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var listSelector = listView.FindDescendant<ListViewBase>()!;
        listSelector.SelectedIndex = 0;
        _ = list.SelectedAsset!.Identity.Should().Be(blue.Identity);
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tiles.InitialLoad.ConfigureAwait(true);
        var tileView = new TilesLayoutView
        {
            ViewModel = tiles,
        };
        await LoadTestContentAsync(tileView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var tileSelector = tileView.FindDescendant<ListViewBase>()!;
        _ = tileSelector.SelectedItem.Should().BeOfType<AssetBrowserRow>().Which.Item.Identity.Should().Be(blue.Identity);
        listSelector.SelectedItem = null;
        tileSelector.SelectedIndex = 1;
        _ = tiles.SelectedAsset!.Identity.Should().Be(red.Identity);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await list.InitialLoad.ConfigureAwait(true);
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = listSelector.SelectedItem.Should().BeOfType<AssetBrowserRow>().Which.Item.Identity.Should().Be(red.Identity);
        var selectedRow = listSelector.SelectedItem;
        var selectedContainer = (Control)listSelector.ContainerFromItem(selectedRow);
        _ = selectedContainer.Focus(FocusState.Keyboard).Should().BeTrue();
        var focused = FocusManager.GetFocusedElement(listView.XamlRoot);
        var published = red with
        {
            RuntimeAvailability = AssetRuntimeAvailability.Mounted,
        };
        updates.OnNext([published, blue]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = listSelector.SelectedItem.Should().BeSameAs(selectedRow);
        _ = list.SelectedAsset.Should().Be(published);
        _ = FocusManager.GetFocusedElement(listView.XamlRoot).Should().BeSameAs(focused);
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>A multi-selection keeps its members and active asset through a re-sort and a switch to the Details table.</summary>
    /// <returns>The asynchronous rendered multi-selection journey.</returns>
    [TestMethod]
    public Task MultiSelectionSurvivesSortAndViewSwitch() => EnqueueAsync(async () =>
    {
        var blue = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        var cube = CreateNavigationAsset("/Content/Geometry/Cube.ogeo", AssetKind.Geometry);
        var red = CreateNavigationAsset("/Content/Materials/Red.omat", AssetKind.Material);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([blue, cube, red]);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        using var tiles = new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        using var table = new DetailsLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tiles.InitialLoad.ConfigureAwait(true);
        var tileView = new TilesLayoutView { ViewModel = tiles };
        await LoadTestContentAsync(tileView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var tileSelector = tileView.FindDescendant<ListViewBase>()!;
        _ = tileSelector.SelectionMode.Should().Be(ListViewSelectionMode.Extended);

        // Ctrl+click Blue, then Red: Red is the active asset.
        tileSelector.SelectedItems.Add(tiles.Assets.Single(row => row.Item == blue));
        tileSelector.SelectedItems.Add(tiles.Assets.Single(row => row.Item == red));
        _ = tiles.SelectedAssets.Should().Equal(blue, red);
        _ = tiles.SelectedAsset.Should().Be(red);
        _ = state.SelectedAssets.Should().Equal(blue, red);

        state.Presentation.SortByCommand.Execute(AssetSortField.Type);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = tiles.Assets[0].Item.Should().Be(cube);
        _ = tiles.SelectedAssets.Should().Equal(blue, red);
        _ = tileSelector.SelectedItems.Should().HaveCount(2);

        await table.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await table.InitialLoad.ConfigureAwait(true);
        var tableView = new DetailsLayoutView { ViewModel = table };
        await LoadTestContentAsync(tableView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var tableSelector = tableView.FindDescendant<ListViewBase>()!;
        _ = tableSelector.SelectedItems.OfType<AssetBrowserRow>().Select(static row => row.Item).Should().BeEquivalentTo([blue, red]);
        _ = table.SelectedAsset.Should().Be(red);
        _ = tableView.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == "Location");
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>Rapid filters and folder navigation cannot restore a hidden command target from an inactive layout.</summary>
    /// <returns>The asynchronous rendered query/selection regression.</returns>
    [TestMethod]
    public Task HiddenExplorerSelectionDoesNotReturnAfterViewSwitch() => EnqueueAsync(async () =>
    {
        var blue = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        var cube = CreateNavigationAsset("/Content/Geometry/Cube.ogeo", AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([blue, cube]);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        using var list = new ListLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        using var tiles = new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await list.InitialLoad.ConfigureAwait(true);
        var listView = new ListLayoutView
        {
            ViewModel = list,
        };
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        listView.FindDescendant<ListViewBase>()!.SelectedIndex = 0;
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tiles.InitialLoad.ConfigureAwait(true);
        var tileView = new TilesLayoutView
        {
            ViewModel = tiles,
        };
        await LoadTestContentAsync(tileView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var invoked = false;
        list.ItemInvoked += (_, _) => invoked = true;
        tiles.ItemInvoked += (_, _) => invoked = true;
        list.InvokeItemCommand.Execute(blue);
        _ = invoked.Should().BeFalse("unloaded layouts cannot invoke their old selection");
        foreach (var text in ExplorerSelectionQueries)
        {
            state.Query.SearchText = text;
        }

        _ = tiles.SelectedAsset.Should().BeNull();
        _ = tileView.FindDescendant<ListViewBase>()!.SelectedItem.Should().BeNull();
        tiles.InvokeItemCommand.Execute(blue);
        _ = invoked.Should().BeFalse();
        state.Query.ClearAllCommand.Execute(parameter: null);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await list.InitialLoad.ConfigureAwait(true);
        await LoadTestContentAsync(listView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = list.SelectedAsset.Should().BeNull();
        _ = listView.FindDescendant<ListViewBase>()!.SelectedItem.Should().BeNull();
        list.SelectedAsset = blue;
        RouteStateMapping.ApplyNavigationScope(state, "/(left:folders//right:assets/list)?selected=%2FContent%2FGeometry");
        RouteStateMapping.ApplyNavigationScope(state, "/(left:folders//right:assets/list)?selected=.");
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tiles.InitialLoad.ConfigureAwait(true);
        _ = tiles.SelectedAsset.Should().BeNull();
    });
}
