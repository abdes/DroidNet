// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises informational tooltips through real browser templates and live status snapshots.</summary>
public sealed partial class InspectorControlTests
{
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
        asset = asset with { SourcePath = "C:/Projects/Example/Content/Materials/BlueMetal.omat.json", DisplayPath = Uri.UnescapeDataString(asset.DisplayPath) };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([asset]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins)
            : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)model } : new ListLayoutView { ViewModel = (ListLayoutViewModel)model };
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
                CookStatus = asset.CookStatus! with { Freshness = AssetCookFreshness.Current, HasPublishedOutput = true, HasVerifiedOutput = true },
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

        await this.CaptureAssetInformationAsync(information.ViewModel!, light, $"asset-information-{tiles}-{light}.png").ConfigureAwait(true);
    });

    private async Task CaptureAssetInformationAsync(AssetInformation information, bool light, string name)
    {
        var border = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Padding='12' Background='{ThemeResource ToolTipBackground}' BorderBrush='{ThemeResource ToolTipBorderBrush}' BorderThickness='1' CornerRadius='4' />");
        border.Child = new AssetInformationView { ViewModel = information };
        var root = new Grid { Width = 420, Height = 360, RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark };
        border.VerticalAlignment = VerticalAlignment.Top;
        root.Children.Add(border);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await this.CaptureQueryLayoutAsync(root, name).ConfigureAwait(true);
    }
}
