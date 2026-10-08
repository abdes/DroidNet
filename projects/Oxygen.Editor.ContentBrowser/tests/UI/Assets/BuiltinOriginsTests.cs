// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserControls;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Assets;

[TestClass]
public sealed partial class BuiltinOriginsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Built-in material and geometry copies stay visible under Cooked and offer no source-cooking action.</summary>
    /// <param name="geometry">Whether to show the cube instead of the default material.</param>
    /// <param name="tiles">Whether to use tiles instead of the list.</param>
    /// <returns>The asynchronous rendered-origin regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(true, true)]
    public Task BuiltinProjectCopiesHaveOwnedLabelsAndContextualCookActions(bool geometry, bool tiles) => EnqueueAsync(async () =>
    {
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var origin = builtins.Snapshot.Catalog!.CreateCatalogRecords().Single(record => string.Equals(record.Name, geometry ? "Cube" : "Default", StringComparison.Ordinal));
        var uri = new Uri("asset://" + origin.Generated!.CookedVirtualPath);
        var item = CreateStatusAsset(0) with
        {
            IdentityUri = uri,
            DisplayPath = uri.AbsolutePath,
            DisplayName = origin.Name,
            Kind = geometry ? AssetKind.Geometry : AssetKind.Material,
            PrimaryState = AssetState.Generated,
            DerivedState = null,
            Generated = origin.Generated,
            BuiltinOriginUri = origin.Uri,
            DescriptorPath = null,
            SourcePath = null,
            CookedUri = uri,
            CookStatus = null,
        };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext { ProjectId = Guid.NewGuid(), ProjectRoot = Path.GetTempPath(), Name = "Built-ins", Category = Category.Games, AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")], LocalFolderMounts = [], Scenes = [] });
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Cooked/Content/" + (geometry ? "Geometry" : "Materials")]);
        using AssetsLayoutViewModel layout = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        FrameworkElement layoutView = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)layout,
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)layout,
        };
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        using var browser = CreateBuiltinBrowserModel(provider.Object, projects, state, layout, layoutView);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        var view = new AssetsView
        {
            ViewModel = browser,
            Width = 800,
            Height = 360,
            RequestedTheme = ElementTheme.Dark,
        };
        var originalSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;
        try
        {
            await LoadTestContentAsync(view).ConfigureAwait(true);
            var scale = view.XamlRoot.RasterizationScale;
            VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(new((int)(view.Width * scale) + 60, (int)(view.Height * scale) + 100));
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = layout.Assets.Should().ContainSingle("classifying a built-in must not remove it from the Cooked folder");
            layout.SelectedAsset = item;
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = layoutView.FindDescendant<TextBlock>(label => string.Equals(label.Text, "Built-in", StringComparison.Ordinal))!.ActualWidth.Should().BePositive();
            if (geometry)
            {
                _ = layoutView.FindDescendant<FontIcon>(icon => string.Equals(icon.Glyph, "\uF158", StringComparison.Ordinal)).Should().NotBeNull();
            }

            var authoredUri = new Uri("asset:///Content/Materials/Authored.omat.json");
            var authored = CreateStatusAsset(0) with
            {
                IdentityUri = authoredUri,
                DisplayPath = authoredUri.AbsolutePath,
            };
            updates.OnNext([item, authored]);
            await WaitForRenderAsync().ConfigureAwait(true);
            await VerifyBuiltinCookingActionsAsync(view, browser, state, layout, authored).ConfigureAwait(true);
        }
        finally
        {
            VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(originalSize);
        }
    });

    private static async Task VerifyBuiltinCookingActionsAsync(AssetsView view, AssetsViewModel browser, ContentBrowserState state, AssetsLayoutViewModel layout, ContentBrowserAssetItem authored)
    {
        var menu = (MenuFlyout)view.FindDescendant<ToolBarButton>(button => string.Equals(button.Label, "Cook", StringComparison.Ordinal))!.Flyout;
        var assetAction = menu.Items.OfType<MenuFlyoutItem>().Single(action => ReferenceEquals(action.Command, browser.CookSelectedAssetCommand));

        // Unavailable entries stay in place, disabled, and say why.
        _ = assetAction.Visibility.Should().Be(Visibility.Visible);
        _ = assetAction.IsEnabled.Should().BeFalse();
        _ = ToolTipService.GetToolTip(assetAction).Should().Be(browser.CookSelectedToolTip);
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = browser.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeFalse();
        state.SetSelectedFolders(["/Content/Materials"]);
        layout.SelectedAsset = authored;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = assetAction.IsEnabled.Should().BeTrue();
        _ = ToolTipService.GetToolTip(assetAction).Should().Be("Cook the selected assets.");
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = browser.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeTrue();
        browser.Dispose();
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
    }
}
