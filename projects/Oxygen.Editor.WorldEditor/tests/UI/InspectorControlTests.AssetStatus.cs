// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Markup;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Tests;

/// <summary>Renders live asset status through the real browser and picker templates.</summary>
public sealed partial class InspectorControlTests
{
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
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, hosting) : new ListLayoutViewModel(provider.Object, projects, state, hosting);
        FrameworkElement view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)model } : new ListLayoutView { ViewModel = (ListLayoutViewModel)model };
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
            items[10] = items[10] with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) };
            updates.OnNext(items.ToArray());
            await WaitForRenderAsync().ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            AssertBrowserRowState(selector, row, container, focus, offset, model, items[10]);
            await this.CaptureComponentLayoutAsync(host, tiles ? "asset-status-tiles.png" : "asset-status-list.png").ConfigureAwait(true);
        }
        finally
        {
            VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(originalSize);
        }
    });

    /// <summary>A visible material choice updates its status without replacing its focused button.</summary>
    /// <returns>The asynchronous picker-focus regression.</returns>
    [TestMethod]
    public Task MaterialStatusUpdatesPreserveTheOpenPickerChoice() => EnqueueAsync(async () =>
    {
        var asset = CreateStatusAsset(0);
        var material = new MaterialPickerResult(
            asset.IdentityUri,
            asset.DisplayName,
            asset.PrimaryState,
            asset.DerivedState,
            asset.RuntimeAvailability,
            asset.DescriptorPath,
            asset.CookedPath,
            BaseColorPreview: null) { CookStatus = asset.CookStatus };
        using var updates = new BehaviorSubject<IReadOnlyList<MaterialPickerResult>>([material]);
        var picker = new Mock<IMaterialPickerService>();
        _ = picker.SetupGet(value => value.Results).Returns(updates);
        _ = picker.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var catalog = new Mock<IAssetCatalog>();
        _ = catalog.SetupGet(value => value.Changes).Returns(System.Reactive.Linq.Observable.Empty<AssetChange>());
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>())).ReturnsAsync([]);
        using var model = new GeometryViewModel(CreateStatusHosting(), catalog.Object, picker.Object) { IsExpanded = true };
        var view = new GeometryView { ViewModel = model, Width = 440 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var owner = (SplitButton)view.FindName("MaterialSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var content = (FrameworkElement)flyout.Content;
            var button = content.FindDescendant<Button>(candidate => candidate.DataContext is MaterialPickerRow row && row.Item.Uri == asset.IdentityUri)!;
            _ = button.Should().NotBeNull();
            var before = button.DataContext;
            _ = button.Focus(FocusState.Programmatic);
            updates.OnNext([material with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = button.DataContext.Should().BeSameAs(before);
            _ = FocusManager.GetFocusedElement(view.XamlRoot).Should().BeSameAs(button);
            var label = button.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Material · Queued", StringComparison.Ordinal));
            _ = label.Should().NotBeNull();
            _ = ToolTipService.GetToolTip(label!).Should().Be("Material · Queued");
        }
        finally
        {
            flyout.Hide();
        }
    });

    private static void AssertBrowserRowState(ListViewBase selector, AssetBrowserRow row, Control container, object? focus, double offset, AssetsLayoutViewModel model, ContentBrowserAssetItem expected)
    {
        _ = selector.SelectedItem.Should().BeSameAs(row);
        _ = selector.ContainerFromItem(row).Should().BeSameAs(container);
        _ = FocusManager.GetFocusedElement(selector.XamlRoot).Should().BeSameAs(focus);
        _ = selector.FindDescendant<ScrollViewer>()!.VerticalOffset.Should().BeApproximately(offset, 1);
        _ = model.SelectedAsset.Should().BeSameAs(expected);
        var status = container.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Queued", StringComparison.Ordinal));
        _ = status.Should().NotBeNull();
        _ = ToolTipService.GetToolTip(status!).Should().Be("Queued");
        var position = status!.TransformToVisual(selector).TransformPoint(new(0, 0));
        _ = position.Y.Should().BeInRange(0, selector.ActualHeight - status.ActualHeight + 1);
    }

    private static HostingContext CreateStatusHosting()
    {
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        return new() { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) };
    }

    private static ContentBrowserAssetItem CreateStatusAsset(int index)
    {
        var name = string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Material {index:00}");
        var uri = new Uri("asset:///Content/" + Uri.EscapeDataString(name) + ".omat.json");
        return new(
            uri,
            name,
            AssetKind.Material,
            AssetState.Descriptor,
            AssetState.Cooked,
            AssetRuntimeAvailability.Unknown,
            uri.AbsolutePath,
            uri.AbsolutePath,
            uri.AbsolutePath,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: true)
        {
            CookStatus = new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], []),
        };
    }
}
