// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Tests;

/// <summary>Drives search and filter controls against both rendered browser layouts.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Search/filter changes and later cook status update both layouts without a new catalog scan.</summary>
    /// <param name="tiles">Whether tiles are the visible layout.</param>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The asynchronous rendered-query regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task BrowserQueryControlsFilterBothLayoutsAndResetEmptyResults(bool tiles, bool light) => EnqueueAsync(async () =>
    {
        var material = CreateQueryAsset("Blue", AssetKind.Material, AssetCookFreshness.NeedsCooking);
        var other = CreateQueryAsset("Red", AssetKind.Material, AssetCookFreshness.Current);
        var mesh = CreateQueryAsset("Blue mesh", AssetKind.Geometry, AssetCookFreshness.NeedsCooking);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([material, other, mesh]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Content"]);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using var list = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        using var tile = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tile.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var queryView = new AssetQueryView { ViewModel = state.Query };
        UserControl assetsView = tiles ? new TilesLayoutView { ViewModel = tile } : new ListLayoutView { ViewModel = list };
        var root = CreateQueryTestRoot(queryView, assetsView, light);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        await SelectBrowserFiltersAsync(queryView, "Materials", "Needs cooking").ConfigureAwait(true);
        _ = state.Query.FilterCount.Should().Be(2);

        var search = (AutoSuggestBox)queryView.FindName("AssetSearch");
        search.Text = "Blue";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = list.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
        _ = tile.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
        updates.OnNext([material with { CookStatus = material.CookStatus! with { Freshness = AssetCookFreshness.Current, HasPublishedOutput = true, HasVerifiedOutput = true } }, other, mesh]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = list.Assets.Should().BeEmpty();
        _ = tile.Assets.Should().BeEmpty();
        _ = list.EmptyTitle.Should().Be("No matching assets");
        var clear = assetsView.FindDescendant<Button>(button => string.Equals(button.Content as string, "Clear search and filters", StringComparison.Ordinal))!;
        _ = clear.Should().NotBeNull();
        await this.CaptureQueryLayoutAsync(root, "browser-query-empty-" + tiles + "-" + light + ".png").ConfigureAwait(true);
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = state.Query.IsActive.Should().BeFalse();
        _ = search.Text.Should().BeEmpty();
        _ = list.Assets.Should().HaveCount(3);
        _ = tile.Assets.Should().HaveCount(3);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Exactly(2));
    });

    /// <summary>An empty cooked folder points to its authored counterpart without scheduling cooking.</summary>
    /// <returns>The asynchronous derived-folder recovery regression.</returns>
    [TestMethod]
    public Task EmptyCookedBrowserCanNavigateToSourceContent() => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([CreateQueryAsset("Blue", AssetKind.Material, AssetCookFreshness.NeedsCooking)]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Cooked/Content/Materials"]);
        using var model = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var view = new ListLayoutView { ViewModel = model };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.EmptyTitle.Should().Be("No cooked assets here");
        var browse = view.FindDescendant<Button>(button => string.Equals(button.Content as string, "Browse source content", StringComparison.Ordinal))!;
        ((IInvokeProvider)new ButtonAutomationPeer(browse).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = state.SelectedFolders.Should().Equal("/Content/Materials");
        _ = model.Assets.Should().ContainSingle();
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
    });

    private static Grid CreateQueryTestRoot(AssetQueryView queryView, UserControl assetsView, bool light)
    {
        var root = new Grid
        {
            Width = 620, Height = 360, RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark,
            Background = new SolidColorBrush(light ? Microsoft.UI.Colors.WhiteSmoke : Microsoft.UI.Colors.Black),
        };
        root.RowDefinitions.Add(new() { Height = GridLength.Auto });
        root.RowDefinitions.Add(new() { Height = new(1, GridUnitType.Star) });
        root.Children.Add(queryView);
        Grid.SetRow(assetsView, 1);
        root.Children.Add(assetsView);
        return root;
    }

    private static async Task SelectBrowserFiltersAsync(AssetQueryView view, params string[] labels)
    {
        var filter = (ToolBarButton)view.FindName("AssetFilterButton");
        var flyout = (Flyout)filter.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(filter);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            foreach (var label in labels)
            {
                SetFilterChoice((FrameworkElement)flyout.Content, label);
            }
        }
        finally
        {
            flyout.Hide();
        }
    }

    private static void SetFilterChoice(FrameworkElement flyout, string label)
    {
        var choice = flyout.FindDescendant<CheckBox>(box => string.Equals(box.Content as string, label, StringComparison.Ordinal))!;
        _ = choice.Should().NotBeNull();
        ((IToggleProvider)new CheckBoxAutomationPeer(choice).GetPattern(PatternInterface.Toggle)).Toggle();
    }

    private static Mock<IContentBrowserAssetProvider> CreateQueryProvider(BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>> updates)
    {
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider;
    }

    private static ProjectContextService CreateQueryProject()
    {
        var projects = new ProjectContextService();
        projects.Activate(new()
        {
            ProjectId = Guid.NewGuid(), Name = "Query", Category = Category.Games, ProjectRoot = "C:/Query",
            AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
        });
        return projects;
    }

    private static ContentBrowserAssetItem CreateQueryAsset(string name, AssetKind kind, AssetCookFreshness freshness)
    {
        var folder = kind == AssetKind.Material ? "Materials" : "Geometry";
        var asset = CreateNavigationAsset("/Content/" + folder + "/" + Uri.EscapeDataString(name), kind);
        var current = freshness == AssetCookFreshness.Current;
        return asset with { DisplayName = name, CookStatus = new(asset.IdentityUri, freshness, HasPublishedOutput: current, HasVerifiedOutput: current, [], [], []) };
    }

    private async Task CaptureQueryLayoutAsync(Grid root, string name)
    {
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var originalSize = window.Size;
        try
        {
            var scale = root.XamlRoot.RasterizationScale;
            window.Resize(new((int)Math.Ceiling(root.Width * scale) + 32, (int)Math.Ceiling(root.Height * scale) + 64));
            await this.CaptureComponentLayoutAsync(root, name).ConfigureAwait(true);
        }
        finally
        {
            window.Resize(originalSize);
        }
    }
}
