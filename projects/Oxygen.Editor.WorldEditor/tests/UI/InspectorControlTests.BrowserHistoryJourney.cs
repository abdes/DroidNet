// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DryIoc;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Qualifies browser history, visible scope and command targets through the real local router.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Physical folder navigation preserves each declared mount alias and reveals collapsed ancestors.</summary>
    /// <param name="mount">The mount representation to navigate.</param>
    /// <returns>The physical-to-virtual navigation regression.</returns>
    [TestMethod]
    [DataRow("Authoring")]
    [DataRow("Cooked")]
    [DataRow("Library")]
    public Task PhysicalFolderNavigationUsesDeclaredMountIdentity(string mount) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture(persisted: true, "Published");
        var (physical, expected) = fixture.ConfigurePhysicalNavigation(mount);
        await fixture.OpenAsync().ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync(physical, this.TestContext.CancellationToken, relativeToContent: false).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Browser.Breadcrumbs[^1].RelativePath.Trim('/').Should().Be(expected);
        _ = fixture.Explorer.SelectedItem!.Label.Should().Be("Materials");
        _ = fixture.Explorer.ShownItems.Should().Contain(fixture.Explorer.SelectedItem);
    });

    /// <summary>All folder/layout history positions expose matching rows, breadcrumbs, selection and cook targets.</summary>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The combined 1,000-entry browser journey.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task BrowserHistoryKeepsThousandEntryQueryAndCookScopesConsistent(bool light) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture();
        var items = fixture.ConfigureHistoryJourney();
        await fixture.OpenAsync().ConfigureAwait(true);
        var view = new ContentBrowserView { ViewModel = fixture.Browser, Width = 1100, Height = 600, RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark };
        var root = new Grid
        {
            Width = view.Width, Height = view.Height, RequestedTheme = view.RequestedTheme,
            Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(light ? Microsoft.UI.Colors.WhiteSmoke : Microsoft.UI.Colors.Black),
        };
        root.Children.Add(view);
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(root, 1, this.TestContext.CancellationToken).ConfigureAwait(true);
        await CheckHistoryQueryControlsAsync(fixture, view).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: true).ConfigureAwait(true);
        await fixture.Browser.SwitchToListViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: false).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Geometry", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Geometry", tiles: false).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Scenes", this.TestContext.CancellationToken).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Scenes", tiles: false).ConfigureAwait(true);
        await fixture.Browser.SwitchToDetailViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await AssertHistoryScopeAsync(fixture, view, items, "Scenes", tiles: true).ConfigureAwait(true);
        foreach (var (folder, tiles) in new[] { ("Scenes", false), ("Geometry", false), ("Materials", false), ("Materials", true) })
        {
            _ = fixture.Browser.CanGoBack.Should().BeTrue();
            await fixture.Browser.GoBackCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await AssertHistoryScopeAsync(fixture, view, items, folder, tiles).ConfigureAwait(true);
        }

        foreach (var (folder, tiles) in new[] { ("Materials", false), ("Geometry", false), ("Scenes", false), ("Scenes", true) })
        {
            _ = fixture.Browser.CanGoForward.Should().BeTrue();
            await fixture.Browser.GoForwardCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await AssertHistoryScopeAsync(fixture, view, items, folder, tiles).ConfigureAwait(true);
        }

        await fixture.Browser.GoBackCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        fixture.PublishHistoryRows(Enumerable.Reverse(items).ToArray());
        await AssertHistoryScopeAsync(fixture, view, items, "Materials", tiles: false).ConfigureAwait(true);
        _ = fixture.Browser.CanGoForward.Should().BeFalse("a new folder after Back replaces the forward branch");
        await this.CaptureQueryLayoutAsync(root, "browser-history-" + light + ".png").ConfigureAwait(true);
    });

    private static async Task CheckHistoryQueryControlsAsync(BrowserRevealFixture fixture, ContentBrowserView view)
    {
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(1000);
        var query = view.FindDescendant<AssetQueryView>()!;
        await SelectBrowserFiltersAsync(query, "Materials", "Scenes").ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(667);
        var search = (AutoSuggestBox)query.FindName("AssetSearch");
        search.Text = "Item00";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Layout.Assets.Should().HaveCount(67);
        _ = fixture.Layout.Assets.Should().OnlyContain(row => row.Item.Kind == AssetKind.Material || row.Item.Kind == AssetKind.Scene);
        fixture.Browser.Query.ClearAllCommand.Execute(parameter: null);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = search.Text.Should().BeEmpty();
        _ = fixture.Layout.Assets.Should().HaveCount(1000);
        _ = fixture.HistoryPipeline.Invocations.Should().BeEmpty("browsing and filtering never submit cooking work");
    }

    private static async Task AssertHistoryScopeAsync(BrowserRevealFixture fixture, ContentBrowserView view, ContentBrowserAssetItem[] all, string folder, bool tiles)
    {
        await WaitForRenderAsync().ConfigureAwait(true);
        var path = "/Content/" + folder;
        var expected = all.Where(item => item.DisplayPath.StartsWith(path + "/", StringComparison.Ordinal)).ToArray();
        _ = fixture.Browser.Breadcrumbs[^1].RelativePath.Trim('/').Should().Be(path.Trim('/'), fixture.Diagnostics);
        _ = fixture.Explorer.SelectedItem!.Label.Should().Be(folder);
        _ = (fixture.Layout is TilesLayoutViewModel).Should().Be(tiles);
        _ = fixture.Layout.Assets.Select(static row => row.Item.IdentityUri).Should().BeEquivalentTo(expected.Select(static item => item.IdentityUri));
        var layoutView = tiles ? (FrameworkElement)view.FindDescendant<TilesLayoutView>()! : view.FindDescendant<ListLayoutView>()!;
        var selector = layoutView.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        var selected = fixture.Layout.SelectedAsset!;
        _ = selected.DisplayPath.Should().StartWith(path + "/");
        _ = selector.SelectedItem.Should().BeSameAs(fixture.Layout.SelectedRow);
        var commands = (AssetsViewModel)fixture.Browser.RightPaneViewModel!;
        _ = commands.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
        await commands.CookSelectedAssetCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = fixture.HistoryPipeline.Invocations[^1].Arguments.Should().HaveElementAt(0, selected.IdentityUri);
        _ = commands.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeTrue();
        await commands.CookSelectedFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = fixture.HistoryPipeline.Invocations[^1].Arguments.Should().HaveElementAt(0, new Uri("asset://" + path));
    }

    private sealed partial class BrowserRevealFixture
    {
        public Mock<IContentPipelineService> HistoryPipeline { get; } = new();

        public ContentBrowserAssetItem[] ConfigureHistoryJourney()
        {
            foreach (var folder in new[] { "Materials", "Geometry", "Scenes" })
            {
                _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content", folder));
            }

            var rows = Enumerable.Range(0, 1000).Select(index =>
            {
                var (folder, suffix, kind) = (index % 3) switch
                {
                    0 => ("Materials", ".omat.json", AssetKind.Material),
                    1 => ("Geometry", ".ogeo.json", AssetKind.Geometry),
                    _ => ("Scenes", ".oscene.json", AssetKind.Scene),
                };
                return CreateNavigationAsset(string.Create(CultureInfo.InvariantCulture, $"/Content/{folder}/Item{index:D4}{suffix}"), kind);
            }).ToArray();
            this.items.OnNext(rows);
            _ = this.HistoryPipeline.Setup(value => value.CookAssetAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>(), It.IsAny<ProjectContext?>()))
                .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
            _ = this.HistoryPipeline.Setup(value => value.CookFolderAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Folder, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
            this.container.Unregister<IContentPipelineService>();
            this.container.RegisterInstance(this.HistoryPipeline.Object);
            return rows;
        }

        public void PublishHistoryRows(IReadOnlyList<ContentBrowserAssetItem> rows) => this.items.OnNext(rows);

        public (string physical, string expected) ConfigurePhysicalNavigation(string mount)
        {
            if (string.Equals(mount, "Library", StringComparison.Ordinal))
            {
                _ = this.SetLibraryOutput();
                return ("Library/Materials", "Library/Materials");
            }

            if (string.Equals(mount, "Cooked", StringComparison.Ordinal))
            {
                return (".cooked/Content/Materials", "Published/Content/Materials");
            }

            this.Projects.Activate(this.Projects.ActiveProject! with { AuthoringMounts = [new("Game", "Content")] });
            return ("Content/Materials", "Game/Materials");
        }

        public async Task NavigateHistoryFolderAsync(string folder, CancellationToken cancellationToken, bool relativeToContent = true)
        {
            var path = relativeToContent ? Path.Combine(this.directory.FullName, "Content", folder) : Path.Combine(this.directory.FullName, folder);
            var location = await this.container.Resolve<DroidNet.Storage.IStorageProvider>().GetFolderFromPathAsync(path, cancellationToken).ConfigureAwait(true);
            await this.Explorer.NavigateToFolderAsync(location).ConfigureAwait(true);
        }
    }
}
