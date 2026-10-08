// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Status;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Assets;

[TestClass]
public sealed partial class AssetInformationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The details pane follows publication without changing selection, browser size or triggering discovery.</summary>
    /// <param name="tiles">Whether to exercise tiles.</param>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The asynchronous details-pane rendering regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task AssetDetailsFollowPublication(bool tiles, bool light) => EnqueueAsync(async () =>
    {
        var asset = CreateQueryAsset("Blue metal with a deliberately long and distinguishable name", AssetKind.Material, AssetCookFreshness.NeedsCooking);
        asset = asset with
        {
            SourcePath = "C:/Projects/Example/Content/Materials/BlueMetal.omat.json",
            DisplayPath = Uri.UnescapeDataString(asset.DisplayPath),
        };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([asset]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        using var details = new AssetDetailsViewModel(state, Mock.Of<IAssetShell>());
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)model,
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)model,
        };
        var pane = new AssetDetailsView { ViewModel = details, Width = 300 };
        var root = new Grid { RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark };
        root.ColumnDefinitions.Add(new ColumnDefinition());
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        root.Children.Add(view);
        Grid.SetColumn(pane, 1);
        root.Children.Add(pane);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var selector = view.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        await WaitForRenderAsync().ConfigureAwait(true);
        var selected = selector.SelectedItem;
        var originalSize = view.ActualSize;

        _ = details.IsSingle.Should().BeTrue();
        _ = details.Title.Should().Be(asset.DisplayName);
        _ = details.Description.Should().Contain("No cooked content yet");
        _ = PaneText(pane).Should().Contain(asset.SourcePath);

        var cooked = asset with
        {
            CookedUri = new("asset:///Content/Materials/BlueMetal.omat"),
            CookStatus = asset.CookStatus! with
            {
                Freshness = AssetCookFreshness.Current,
                HasPublishedOutput = true,
                OutputAvailability = CookedOutputAvailability.Present,
            },
        };
        updates.OnNext([cooked]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = details.Description.Should().Contain("No cooking is needed");
        _ = PaneText(pane).Should().Contain(cooked.CookedUri.AbsolutePath);
        _ = selector.SelectedItem.Should().BeSameAs(selected);
        _ = view.ActualSize.Should().Be(originalSize);
        provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
    });

    // Paths wrap at separators through zero-width breaks; compare the text a reader sees.
    private static IEnumerable<string> PaneText(FrameworkElement pane)
        => pane.FindDescendants().OfType<TextBlock>().Select(static text => text.Text.Replace("\u200B", string.Empty, StringComparison.Ordinal));
}
