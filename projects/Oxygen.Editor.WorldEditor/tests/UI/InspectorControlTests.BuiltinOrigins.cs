// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks built-in project copies in the real browser layouts and cooking menu.</summary>
public sealed partial class InspectorControlTests
{
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
            IdentityUri = uri, DisplayPath = uri.AbsolutePath, DisplayName = origin.Name, Kind = geometry ? AssetKind.Geometry : AssetKind.Material,
            PrimaryState = AssetState.Generated, DerivedState = null, Generated = origin.Generated, BuiltinOriginUri = origin.Uri,
            DescriptorPath = null, SourcePath = null, CookedUri = uri, CookStatus = null,
        };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext { ProjectId = Guid.NewGuid(), ProjectRoot = Path.GetTempPath(), Name = "Built-ins", Category = Category.Games, AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [] });
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Cooked/Content/" + (geometry ? "Geometry" : "Materials")]);
        using AssetsLayoutViewModel layout = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), builtins);
        FrameworkElement layoutView = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)layout } : new ListLayoutView { ViewModel = (ListLayoutViewModel)layout };
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        using var browser = CreateBuiltinBrowserModel(provider.Object, projects, state, layout, layoutView);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        var view = new AssetsView { ViewModel = browser, Width = 800, Height = 360, RequestedTheme = ElementTheme.Dark };
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

            await this.CaptureComponentLayoutAsync(view, $"builtin-origin-{geometry}-{tiles}.png").ConfigureAwait(true);
            var authoredUri = new Uri("asset:///Content/Materials/Authored.omat.json");
            var authored = CreateStatusAsset(0) with { IdentityUri = authoredUri, DisplayPath = authoredUri.AbsolutePath };
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
        _ = assetAction.Visibility.Should().Be(Visibility.Collapsed);
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = browser.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeFalse();
        state.SetSelectedFolders(["/Content/Materials"]);
        layout.SelectedAsset = authored;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = assetAction.Visibility.Should().Be(Visibility.Visible);
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = browser.CookSelectedFolderCommand.CanExecute(parameter: null).Should().BeTrue();
        browser.Dispose();
        _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    private static AssetsViewModel CreateBuiltinBrowserModel(IContentBrowserAssetProvider provider, ProjectContextService projects, ContentBrowserState state, AssetsLayoutViewModel layout, FrameworkElement layoutView, IMessenger? messenger = null, IContentPipelineService? pipeline = null)
    {
        var catalog = new Mock<IAssetCatalog>();
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>())).ReturnsAsync([]);
        var locator = new Mock<DroidNet.Mvvm.IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(layout)).Returns(layoutView);
        return new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.ICookRunService>(),
            catalog.Object,
            new DroidNet.Mvvm.Converters.ViewModelToView(locator.Object),
            state,
            projects,
            Mock.Of<IProjectManagerService>(),
            Mock.Of<IAuthoringTargetResolver>(),
            pipeline ?? Mock.Of<IContentPipelineService>(),
            provider,
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            messenger ?? new StrongReferenceMessenger(),
            Mock.Of<DroidNet.Aura.Dialogs.IDialogService>(),
            Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>());
    }
}
