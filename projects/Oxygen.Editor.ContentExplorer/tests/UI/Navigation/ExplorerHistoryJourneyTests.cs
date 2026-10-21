// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerControls;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

[TestClass]
public sealed partial class ExplorerHistoryJourneyTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Physical folder navigation preserves each declared mount alias and reveals collapsed ancestors.</summary>
    /// <param name="mount">The mount representation to navigate.</param>
    /// <returns>The physical-to-virtual navigation regression.</returns>
    [TestMethod]
    [DataRow("Authoring")]
    [DataRow("Cooked")]
    [DataRow("Library")]
    public Task PhysicalFolderNavigationUsesDeclaredMountIdentity(string mount) => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture(persisted: true, "Published");
        var (physical, expected) = await fixture.ConfigurePhysicalNavigationAsync(mount, this.TestContext.CancellationToken).ConfigureAwait(true);
        await fixture.OpenAsync().ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync(physical, this.TestContext.CancellationToken, relativeToContent: false).ConfigureAwait(true);
        _ = fixture.Explorer.Breadcrumbs[^1].Folder.Should().Be(AssetPath.Parse("/" + expected), fixture.Diagnostics);
        _ = fixture.Folders.SelectedItem!.Label.Should().Be("Materials");
        _ = fixture.Folders.ShownItems.Should().Contain(fixture.Folders.SelectedItem);
    });

    /// <summary>All folder/layout history positions expose matching rows, breadcrumbs, selection and cook targets.</summary>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The combined 1,000-entry browser journey.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ExplorerHistoryKeepsThousandEntryQueryAndCookScopesConsistent(bool light) => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        var items = fixture.ConfigureHistoryJourney();
        await fixture.OpenAsync().ConfigureAwait(true);
        var view = new ContentExplorerView
        {
            ViewModel = fixture.Explorer,
            Width = 1100,
            Height = 600,
            RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark,
        };
        var root = new Grid
        {
            Width = view.Width,
            Height = view.Height,
            RequestedTheme = view.RequestedTheme,
            Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(light ? Microsoft.UI.Colors.WhiteSmoke : Microsoft.UI.Colors.Black),
        };
        root.Children.Add(view);
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(root, 1, this.TestContext.CancellationToken).ConfigureAwait(true);

        // The project opens on its Scenes folder; the query runs over the whole project.
        await fixture.Explorer.NavigateToBreadcrumbAsync(0).ConfigureAwait(true);
        await CheckHistoryQueryControlsAsync(fixture, view).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: true).ConfigureAwait(true);
        await fixture.Explorer.SwitchToListViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: false).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Geometry", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Geometry", tiles: false).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Scenes", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Scenes", tiles: false).ConfigureAwait(true);
        await fixture.Explorer.SwitchToTilesViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Scenes", tiles: true).ConfigureAwait(true);
        foreach (var (folder, tiles) in new[]
        {
            ("Scenes", false),
            ("Geometry", false),
            ("Materials", false),
            ("Materials", true),
        }

        )
        {
            _ = fixture.Explorer.CanGoBack.Should().BeTrue();
            await fixture.Explorer.GoBackCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await AssertHistoryScopeAsync(fixture, view, items, folder, tiles).ConfigureAwait(true);
        }

        foreach (var (folder, tiles) in new[]
        {
            ("Materials", false),
            ("Geometry", false),
            ("Scenes", false),
            ("Scenes", true),
        }

        )
        {
            _ = fixture.Explorer.CanGoForward.Should().BeTrue();
            await fixture.Explorer.GoForwardCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await AssertHistoryScopeAsync(fixture, view, items, folder, tiles).ConfigureAwait(true);
        }

        await fixture.Explorer.GoBackCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        fixture.PublishHistoryRows(Enumerable.Reverse(items).ToArray());
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: false).ConfigureAwait(true);
        _ = fixture.Explorer.CanGoForward.Should().BeFalse("a new folder after Back replaces the forward branch");
    });

    private static async Task CheckHistoryQueryControlsAsync(ExplorerRevealFixture fixture, ContentExplorerView view)
    {
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(1000);
        var query = view.FindDescendant<AssetQueryView>()!;
        await SelectExplorerFiltersAsync(query, "Materials", "Scenes").ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(667);
        var search = (AutoSuggestBox)query.FindName("AssetSearch");
        search.Text = "Item00";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(67);
        _ = fixture.Layout.Assets.Should().OnlyContain(row => row.Item.Kind == AssetKind.Material || row.Item.Kind == AssetKind.Scene);
        fixture.Explorer.Query.ClearAllCommand.Execute(parameter: null);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = search.Text.Should().BeEmpty();
        _ = fixture.Layout.Assets.Should().HaveCount(1000);
        _ = fixture.HistoryPipeline.Invocations.Should().BeEmpty("browsing and filtering never submit cooking work");
    }

    private static async Task AssertHistoryScopeAsync(ExplorerRevealFixture fixture, ContentExplorerView view, AssetEntry[] all, string folder, bool tiles)
    {
        await WaitForRenderAsync().ConfigureAwait(true);
        var path = "/Content/" + folder;
        var expected = all.Where(item => item.DisplayPath.StartsWith(path + "/", StringComparison.Ordinal)).ToArray();
        _ = fixture.Explorer.Breadcrumbs[^1].Folder.Should().Be(AssetPath.Parse(path), fixture.Diagnostics);
        _ = fixture.Folders.SelectedItem!.Label.Should().Be(folder);
        _ = (fixture.Layout is TilesLayoutViewModel).Should().Be(tiles);
        _ = fixture.Layout.Assets.Select(static row => row.Item.Identity).Should().BeEquivalentTo(expected.Select(static item => item.Identity));
        var layoutView = tiles ? (FrameworkElement)view.FindDescendant<TilesLayoutView>()! : view.FindDescendant<ListLayoutView>()!;
        var selector = layoutView.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        var selected = fixture.Layout.SelectedAsset!;
        _ = selected.DisplayPath.Should().StartWith(path + "/");
        _ = selector.SelectedItem.Should().BeSameAs(fixture.Layout.SelectedRow);
        var commands = (AssetsViewModel)fixture.Explorer.RightPaneViewModel!;
        _ = commands.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
        await commands.CookSelectedAssetCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = fixture.HistoryPipeline.Invocations[^1].Arguments.Should().HaveElementAt(0, selected.Identity);
        _ = commands.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeTrue();
        await commands.CookSelectedFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = fixture.HistoryPipeline.Invocations[^1].Arguments.Should().HaveElementAt(0, AssetPath.Parse(path));
    }
}
