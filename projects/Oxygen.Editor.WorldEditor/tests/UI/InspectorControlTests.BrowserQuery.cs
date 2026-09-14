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
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>Drives search and filter controls against both rendered browser layouts.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Search/filter changes and later cook status update both layouts without a new catalog scan.</summary>
    /// <param name="tiles">Whether tiles are the visible layout.</param>
    /// <param name="light">Whether to render the light theme.</param>
    /// <param name="rasterizationScale">The effective XAML scale.</param>
    /// <returns>The asynchronous rendered-query regression.</returns>
    [TestMethod]
    [DataRow(false, false, 1d)]
    [DataRow(false, false, 1.5d)]
    [DataRow(false, false, 2d)]
    [DataRow(false, true, 1d)]
    [DataRow(false, true, 1.5d)]
    [DataRow(false, true, 2d)]
    [DataRow(true, false, 1d)]
    [DataRow(true, false, 1.5d)]
    [DataRow(true, false, 2d)]
    [DataRow(true, true, 1d)]
    [DataRow(true, true, 1.5d)]
    [DataRow(true, true, 2d)]
    public Task BrowserQueryControlsFilterBothLayoutsAndResetEmptyResults(bool tiles, bool light, double rasterizationScale) => EnqueueAsync(async () =>
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
        root.Width = light ? 360 : 620;
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(root, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        await SelectBrowserFiltersAsync(queryView, "Materials", "Needs cooking").ConfigureAwait(true);
        _ = state.Query.FilterCount.Should().Be(2);

        var search = (AutoSuggestBox)queryView.FindName("AssetSearch");
        _ = search.Focus(FocusState.Keyboard).Should().BeTrue();
        _ = Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(search).Should().Be("Search assets");
        search.Text = "Blue";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = list.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
        _ = tile.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
        updates.OnNext([material with { CookStatus = material.CookStatus! with { Freshness = AssetCookFreshness.Current, HasPublishedOutput = true, HasVerifiedOutput = true } }, other, mesh]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var clear = FindEmptyQueryReset(list, tile, assetsView);
        await this.CaptureQueryLayoutAsync(root, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"browser-query-empty-{tiles}-{light}-{rasterizationScale}.png")).ConfigureAwait(true);
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = state.Query.IsActive.Should().BeFalse();
        _ = search.Text.Should().BeEmpty();
        _ = list.Assets.Should().HaveCount(3);
        _ = tile.Assets.Should().HaveCount(3);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
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

    /// <summary>Materials-and-scenes filtering shows one Default for its built-in and cooked representations.</summary>
    /// <param name="tiles">Whether to render tiles.</param>
    /// <returns>The asynchronous duplicate-row regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task MaterialAndSceneFiltersShowDefaultOnceAcrossScopes(bool tiles) => EnqueueAsync(async () =>
    {
        var origin = CreateQueryAsset("Default", AssetKind.Material, AssetCookFreshness.Current) with
        {
            IdentityUri = AssetUris.BuildGeneratedUri("Materials/Default"), DisplayPath = "/Engine/Generated/Materials/Default",
            PrimaryState = AssetState.Generated, SourcePath = null, DescriptorPath = null, CookStatus = null,
            Generated = new("Default", "oxygen.material-descriptor.v1", "/Content/Materials/OxygenEditor_Default.omat"),
        };
        var copy = origin with
        {
            IdentityUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"), BuiltinOriginUri = origin.IdentityUri,
            DisplayPath = "/Content/Materials/OxygenEditor_Default.omat", CookedUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"),
            CookedPath = "C:/Query/.cooked/Content/Materials/OxygenEditor_Default.omat",
        };
        var scenes = new[] { CreateQueryAsset("Main", AssetKind.Scene, AssetCookFreshness.Current), CreateQueryAsset("Second", AssetKind.Scene, AssetCookFreshness.Current) };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([origin, copy, .. scenes]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        state.Query.TypeOptions.Single(option => string.Equals(option.Label, "Materials", StringComparison.Ordinal)).IsSelected = true;
        state.Query.TypeOptions.Single(option => string.Equals(option.Label, "Scenes", StringComparison.Ordinal)).IsSelected = true;
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins)
            : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)model } : new ListLayoutView { ViewModel = (ListLayoutViewModel)model };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.Assets.Should().HaveCount(3);
        _ = model.Assets.Should().ContainSingle(row => row.Item.IsBuiltin);
        model.SelectedAsset = copy;
        _ = model.SelectedAsset!.IdentityUri.Should().Be(origin.IdentityUri);
        updates.OnNext([copy, .. scenes, origin]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.Assets.Should().HaveCount(3);
        _ = model.SelectedAsset!.IdentityUri.Should().Be(origin.IdentityUri);
        state.SetSelectedFolders(["/Cooked/Content/Materials"]);
        _ = model.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(copy.IdentityUri);
    });

    private static Button FindEmptyQueryReset(ListLayoutViewModel list, TilesLayoutViewModel tile, UserControl assetsView)
    {
        _ = list.Assets.Should().BeEmpty();
        _ = tile.Assets.Should().BeEmpty();
        _ = list.EmptyTitle.Should().Be("No matching assets");
        var clear = assetsView.FindDescendant<Button>(button => string.Equals(button.Content as string, "Clear search and filters", StringComparison.Ordinal))!;
        _ = clear.Should().NotBeNull();
        return clear;
    }

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
