// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.TestSupport;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserControls;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Assets;

[TestClass]
public sealed partial class AssetStatusTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Changing status preserves selected containers, focus and scroll in both browser layouts.</summary>
    /// <param name="tiles">Whether to exercise tiles instead of rows.</param>
    /// <returns>The asynchronous rendered-layout regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task AssetStatusUpdatesPreserveBrowserSelectionAndScroll(bool tiles) => EnqueueAsync(async () =>
    {
        var items = Enumerable.Range(0, 20).Select(CreateStatusAsset).ToArray();
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>(items);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var projects = new ProjectContextService();
        var state = new ContentBrowserState(projects);
        var hosting = CreateStatusHosting();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, hosting, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture()) : new ListLayoutViewModel(provider.Object, projects, state, hosting, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        FrameworkElement view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)model
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)model
        };
        var host = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' />");
        host.Width = 460;
        host.Height = 340;
        host.RequestedTheme = ElementTheme.Dark;
        host.Child = view;
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var originalSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;
        var scale = host.XamlRoot.RasterizationScale;
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(new((int)(host.Width * scale) + 60, (int)(host.Height * scale) + 100));
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var selector = view.FindDescendant<ListViewBase>()!;
            var row = model.Assets[10];
            selector.ScrollIntoView(row);
            selector.SelectedItem = row;
            await WaitForRenderAsync().ConfigureAwait(true);
            var container = (Control)selector.ContainerFromItem(row);
            _ = container.Focus(FocusState.Programmatic);
            await WaitForRenderAsync().ConfigureAwait(true);
            var focus = FocusManager.GetFocusedElement(host.XamlRoot);
            var scroll = selector.FindDescendant<ScrollViewer>()!;
            var offset = scroll.VerticalOffset;
            items[10] = items[10] with
            {
                CookActivity = new(Guid.NewGuid(), CookRunState.Queued)
            };
            updates.OnNext(items.ToArray());
            await WaitForRenderAsync().ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            AssertBrowserRowState(selector, row, container, focus, offset, model, items[10]);
        }
        finally
        {
            VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(originalSize);
        }
    });
}
