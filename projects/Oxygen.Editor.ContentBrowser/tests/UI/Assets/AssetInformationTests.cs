// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.TestSupport;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Assets;

[TestClass]
public sealed partial class AssetInformationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Supplemental information follows publication without changing selection, browser size or triggering discovery.</summary>
    /// <param name="tiles">Whether to exercise tiles.</param>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The asynchronous tooltip and component-rendering regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task AssetInformationTooltipFollowsPublication(bool tiles, bool light) => EnqueueAsync(async () =>
    {
        var asset = CreateQueryAsset("Blue metal with a deliberately long and distinguishable name", AssetKind.Material, AssetCookFreshness.NeedsCooking);
        asset = asset with
        {
            SourcePath = "C:/Projects/Example/Content/Materials/BlueMetal.omat.json",
            DisplayPath = Uri.UnescapeDataString(asset.DisplayPath)
        };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([asset]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)model
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)model
        };
        view.RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark;
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var selector = view.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        var selected = selector.SelectedItem;
        var originalSize = view.ActualSize;
        var tooltip = view.FindDescendants().OfType<FrameworkElement>().Select(ToolTipService.GetToolTip).OfType<ToolTip>().Single(tip => tip.Content is AssetInformationView);
        var information = (AssetInformationView)tooltip.Content;
        tooltip.IsOpen = true;
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = tooltip.IsOpen.Should().BeTrue();
            _ = information.ViewModel!.Title.Should().Contain(asset.DisplayName);
            _ = information.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == asset.SourcePath);
            _ = information.ViewModel.Description.Should().Contain("No cooked content yet");
            var cooked = asset with
            {
                CookedUri = new("asset:///Content/Materials/BlueMetal.omat"),
                CookStatus = asset.CookStatus! with
                {
                    Freshness = AssetCookFreshness.Current,
                    HasPublishedOutput = true,
                    OutputAvailability = CookedOutputAvailability.Present
                },
            };
            updates.OnNext([cooked]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = information.ViewModel.Description.Should().Contain("No cooking is needed");
            _ = information.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == cooked.CookedUri.AbsolutePath);
            _ = selector.SelectedItem.Should().BeSameAs(selected);
            _ = view.ActualSize.Should().Be(originalSize);
            provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
        }
        finally
        {
            tooltip.IsOpen = false;
        }
    });
}
