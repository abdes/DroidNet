// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserControls;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Navigation;

[TestClass]
public sealed partial class BrowserQueryTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

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
        var queryView = new AssetQueryView
        {
            ViewModel = state.Query,
        };
        UserControl assetsView = tiles ? new TilesLayoutView
        {
            ViewModel = tile,
        }

        : new ListLayoutView
        {
            ViewModel = list,
        };
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
        updates.OnNext([material with { CookStatus = material.CookStatus! with { Freshness = AssetCookFreshness.Current, HasPublishedOutput = true, OutputAvailability = CookedOutputAvailability.Present } }, other, mesh]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var clear = FindEmptyQueryReset(list, tile, assetsView);
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
        var projects = CreateQueryProject(cookedMount: true);
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Cooked/Content/Materials"]);
        using var model = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var view = new ListLayoutView
        {
            ViewModel = model,
        };
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
            IdentityUri = AssetUris.BuildGeneratedUri("Materials/Default"),
            DisplayPath = "/Engine/Generated/Materials/Default",
            PrimaryState = AssetState.Generated,
            SourcePath = null,
            DescriptorPath = null,
            CookStatus = null,
            Generated = new("Default", "oxygen.material-descriptor.v1", "/Content/Materials/OxygenEditor_Default.omat", GeneratedAssetCategory.Standard),
        };
        var copy = origin with
        {
            IdentityUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"),
            BuiltinOriginUri = origin.IdentityUri,
            DisplayPath = "/Content/Materials/OxygenEditor_Default.omat",
            CookedUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"),
            CookedPath = "C:/Query/.cooked/Content/Materials/OxygenEditor_Default.omat",
        };
        var scenes = new[]
        {
            CreateQueryAsset("Main", AssetKind.Scene, AssetCookFreshness.Current),
            CreateQueryAsset("Second", AssetKind.Scene, AssetCookFreshness.Current),
        };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([origin, copy, .. scenes]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject(cookedMount: true);
        var state = new ContentBrowserState(projects);
        state.Query.TypeOptions.Single(option => string.Equals(option.Label, "Materials", StringComparison.Ordinal)).IsSelected = true;
        state.Query.TypeOptions.Single(option => string.Equals(option.Label, "Scenes", StringComparison.Ordinal)).IsSelected = true;
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)model,
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)model,
        };
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
}
