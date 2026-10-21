// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserControls;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

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
        var source = CreateImportedSourceRow(configured: false);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([source]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState();
        using AssetsLayoutViewModel model = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting()) : new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());
        using var details = new AssetDetailsViewModel(state, projects, Mock.Of<IDesktopIntegration>());
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
        updates.OnNext([CreateImportedSourceRow(configured: true)]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var names = details.Facts.Single(static fact => string.Equals(fact.Label, "Outputs", StringComparison.Ordinal)).Value;
        _ = names.Should().Contain("Main (Geometry)").And.Contain("Paint (Material)").And.Contain("model (Scene)");
        _ = selector.SelectedItem.Should().BeSameAs(selected);
        _ = pane.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == names);
        provider.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once);
    });

    /// <summary>The geometry picker begins empty for a source model, then offers only its geometry output.</summary>
    /// <returns>The asynchronous typed-picker regression.</returns>
    [TestMethod]
    public Task ImportedOutputsPopulateOnlyMatchingPickerTypes() => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([CreateImportedSourceRow(configured: false)]);
        var provider = CreateQueryProvider(updates);
        using var slots = new FakeMaterialSlotProvider();
        using var geometry = new GeometryViewModel(CreateStatusHosting(), provider.Object, slots, Mock.Of<Oxygen.Editor.Projects.IProjectContextService>());
        var view = new GeometryView
        {
            ViewModel = geometry,
            Width = 440,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = geometry.GeometryChoices.Groups.Should().NotContain(static group => group.Origin == AssetOrigin.Content);
        var source = CreateImportedSourceRow(configured: true);
        var outputs = CreateImportedOutputRows(source);
        updates.OnNext([source, .. outputs]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var content = geometry.GeometryChoices.Groups.Single(static group => group.Origin == AssetOrigin.Content);
        _ = content.Rows.Should().ContainSingle().Which.Choice.Asset.Should().Be(outputs.Single(static item => item.Kind == AssetKind.Geometry).Identity);
        var material = outputs.Single(static item => item.Kind == AssetKind.Material).Identity;
        _ = content.Rows.Should().NotContain(row => row.Choice.Asset == material || row.Choice.Asset == source.Identity);
    });

    /// <summary>
    /// A model is one row in the browser, its products are not listed: the Import menu reimports the selected model,
    /// and inspection opens the model's cooked output.
    /// </summary>
    /// <returns>The asynchronous rendered action regression.</returns>
    [TestMethod]
    public Task ImportedModelCommandsReimportAndInspectTheSelectedModel() => EnqueueAsync(async () =>
    {
        var source = CreateImportedSourceRow(configured: true);
        var output = CreateImportedOutputRows(source).Single(static row => row.Kind == AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([source, output]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState();
        var messenger = new StrongReferenceMessenger();
        var reveal = new Mock<IAssetReveal>();
        AssetPath? inspected = null;
        messenger.Register<OpenCookedInspectionRequestMessage>(this, (_, request) =>
        {
            inspected = request.Asset;
            request.Reply(Task.FromResult(true));
        });
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.ReimportSourceAsync(source.Identity, projects.ActiveProject!, It.IsAny<CancellationToken>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        using var layout = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());
        var layoutView = new TilesLayoutView
        {
            ViewModel = layout,
        };
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        using var browser = CreateBrowserModel(provider.Object, projects, state, layout, layoutView, messenger, pipeline.Object, assetReveal: reveal.Object);
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
        pipeline.Verify(value => value.ReimportSourceAsync(source.Identity, projects.ActiveProject!, It.IsAny<CancellationToken>()), Times.Once);
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

    private static AssetEntry CreateImportedSourceRow(bool configured)
    {
        var asset = AssetPath.Parse("/Content/Models/Crate/model.gltf");
        return new(asset, "model.gltf", AssetKind.ForeignSource, AssetState.Source, DerivedState: null, AssetRuntimeAvailability.NotApplicable, asset.VirtualPath, FileSystemPath.From("C:/Project/Content/Models/Crate/model.gltf"), DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            ImportSource = configured ? asset : null,
            CookStatus = configured ? new(asset, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/Main.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Models/Crate/model.gltf/Main.ogeo"), new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/Paint.omat"), ContentCookAssetKind.Material, "Content", "/Content/Models/Crate/model.gltf/Paint.omat"), new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/model.oscene"), ContentCookAssetKind.Scene, "Content", "/Content/Models/Crate/model.gltf/model.oscene"),], []) : null,
        };
    }

    private static AssetEntry[] CreateImportedOutputRows(AssetEntry source)
        => [.. source.CookStatus!.Outputs.Select(
        output =>
        {
            var assetKind = output.Kind switch
            {
                ContentCookAssetKind.Geometry => AssetKind.Geometry,
                ContentCookAssetKind.Material => AssetKind.Material,
                _ => AssetKind.Scene,
            };

            return new AssetEntry(
                output.CookedAssetUri,
                Path.GetFileNameWithoutExtension(output.VirtualPath),
                assetKind,
                AssetState.Cooked,
                DerivedState: null,
                AssetRuntimeAvailability.NotMounted,
                output.VirtualPath,
                SourcePath: null,
                DescriptorPath: null,
                CookedPath: null,
                AssetGuid: null,
                [],
                IsSelectable: true)
                {
                    ImportSource = source.Identity,
                    CookStatus = source.CookStatus with { AssetUri = output.CookedAssetUri },
                };
        })];
}
