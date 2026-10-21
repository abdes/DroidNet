// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Testing;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerControls;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Assets;

[TestClass]
internal sealed class ImportedAssetsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source model's details update in place with named output types after import.</summary>
    /// <param name="tiles">Whether to use tiles or list rows.</param>
    /// <param name="light">Whether to capture light theme.</param>
    /// <returns>The asynchronous details-pane regression.</returns>
    [TestMethod]
    [DataRow(true, false)]
    [DataRow(false, true)]
    public Task ImportedModelDetailsShowNamedOutputs(bool tiles, bool light) => EnqueueAsync(async () =>
    {
        var source = TestAssetEntries.ImportedModel(imported: false);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([source]);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting()) : new ListLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        using var details = new AssetDetailsViewModel(state, session, Mock.Of<IDesktopIntegration>());
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
        _ = details.Description.Should().Contain("Import this model");
        updates.OnNext([TestAssetEntries.ImportedModel(imported: true)]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var names = details.Facts.Single(static fact => string.Equals(fact.Label, "Outputs", StringComparison.Ordinal)).Value;
        _ = names.Should().Contain("Main (Geometry)").And.Contain("Paint (Material)").And.Contain("model (Scene)");
        _ = selector.SelectedItem.Should().BeSameAs(selected);
        _ = pane.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == names);
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>
    /// A model is one row in the browser, its products are not listed: the Import menu reimports the selected model,
    /// and inspection opens the model's cooked output.
    /// </summary>
    /// <returns>The asynchronous rendered action regression.</returns>
    [TestMethod]
    public Task ImportedModelCommandsReimportAndInspectTheSelectedModel() => EnqueueAsync(async () =>
    {
        var source = TestAssetEntries.ImportedModel(imported: true);
        var output = TestAssetEntries.ImportedOutputs(source).Single(static row => row.Kind == AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([source, output]);
        var provider = CreateQueryProvider(updates);
        using var session = CreateQuerySession();
        var state = new ContentExplorerState();
        var reveal = new Mock<IAssetReveal>();
        AssetPath? inspected = null;
        var inspections = new Mock<ICookedInspections>();
        _ = inspections.Setup(value => value.InspectAsync(It.IsAny<CookedInspectionRequest>()))
            .Callback((CookedInspectionRequest request) => inspected = request.Asset)
            .ReturnsAsync(true);
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.ReimportSourceAsync(source.Identity, session.Context, It.IsAny<CancellationToken>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        using var layout = new TilesLayoutViewModel(provider.Object, session, state, CreateStatusHosting());
        var layoutView = new TilesLayoutView
        {
            ViewModel = layout,
        };
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        using var browser = CreateAssetsModel(provider.Object, session, state, layout, layoutView, pipeline.Object, assetReveal: reveal.Object, cookedInspections: inspections.Object);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        var view = new AssetsView
        {
            ViewModel = browser,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        _ = layout.Assets.Select(static row => row.Item.Identity).Should().Equal(source.Identity);
        layout.SelectedAsset = source;
        await WaitForRenderAsync().ConfigureAwait(true);
        await InvokeImportMenuActionAsync(view, "Reimport", () => browser.ReimportSelectedSourceCommand.ExecutionTask).ConfigureAwait(true);
        pipeline.Verify(value => value.ReimportSourceAsync(source.Identity, session.Context, It.IsAny<CancellationToken>()), Times.Once);
        await browser.InspectCookedOutputCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = inspected.Should().Be(source.Identity);
        reveal.Verify(value => value.RevealAsync(It.IsAny<IReadOnlyList<AssetPath>>()), Times.Never);
    });

    private static async Task InvokeImportMenuActionAsync(AssetsView view, string text, Func<Task?> execution)
    {
        var button = view.FindDescendant<ToolBarButton>(item => string.Equals(item.Label, "Import", StringComparison.Ordinal))!;
        var menu = (MenuFlyout)button.Flyout;
        menu.ShowAt(button);
        await WaitForRenderAsync().ConfigureAwait(true);
        var item = menu.Items.OfType<MenuFlyoutItem>().Single(item => string.Equals(item.Text, text, StringComparison.Ordinal));
        _ = item.Visibility.Should().Be(Visibility.Visible);
        ((IInvokeProvider)new MenuFlyoutItemAutomationPeer(item).GetPattern(PatternInterface.Invoke)).Invoke();
        await execution()!.ConfigureAwait(true);
    }
}
