// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises imported-source information, typed choices and commands through the rendered browser.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Source tooltips update in place with named output types after import.</summary>
    /// <param name="tiles">Whether to use tiles or list rows.</param>
    /// <param name="light">Whether to capture light theme.</param>
    /// <returns>The asynchronous tooltip regression.</returns>
    [TestMethod]
    [DataRow(true, false)]
    [DataRow(false, true)]
    public Task ImportedModelTooltipShowsNamedOutputs(bool tiles, bool light) => EnqueueAsync(async () =>
    {
        var source = CreateImportedSourceRow(configured: false);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([source]);
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
        var tip = view.FindDescendants().OfType<FrameworkElement>().Select(ToolTipService.GetToolTip).OfType<ToolTip>().Single(value => value.Content is AssetInformationView);
        tip.IsOpen = true;
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var information = (AssetInformationView)tip.Content;
            _ = information.ViewModel!.Description.Should().Contain("Import this model");
            updates.OnNext([CreateImportedSourceRow(configured: true)]);
            await WaitForRenderAsync().ConfigureAwait(true);
            var names = information.ViewModel!.Facts.Single(static fact => string.Equals(fact.Label, "Outputs", StringComparison.Ordinal)).Value;
            _ = names.Should().Contain("Main (Geometry)").And.Contain("Paint (Material)").And.Contain("Crate (Scene)");
            _ = selector.SelectedItem.Should().BeSameAs(selected);
            _ = information.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == names);
            provider.Verify(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>()), Times.Once);
        }
        finally
        {
            tip.IsOpen = false;
        }

        await this.CaptureAssetInformationAsync(CreateImportedSourceRow(configured: true).Information, light, "imported-source-tooltip-" + light + ".png").ConfigureAwait(true);
    });

    /// <summary>Picker content begins empty for source models, then shows only actual geometry and material outputs.</summary>
    /// <returns>The asynchronous typed-picker regression.</returns>
    [TestMethod]
    public Task ImportedOutputsPopulateOnlyMatchingPickerTypes() => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([CreateImportedSourceRow(configured: false)]);
        var provider = CreateQueryProvider(updates);
        using var materials = new MaterialPickerService(provider.Object);
        using var geometry = new GeometryViewModel(CreateStatusHosting(), provider.Object, materials, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), Mock.Of<Services.ISceneContentDemandService>());
        var view = new GeometryView { ViewModel = geometry, Width = 440 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var content = geometry.Groups.Single(static group => string.Equals(group.Key, "Content", StringComparison.Ordinal));
        _ = content.Items.Should().BeEmpty();
        var source = CreateImportedSourceRow(configured: true);
        var outputs = CreateImportedOutputRows(source);
        updates.OnNext([source, .. outputs]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = content.Items.Should().ContainSingle().Which.Item.Uri.Should().Be(outputs.Single(static item => item.Kind == AssetKind.Geometry).IdentityUri);
        var material = await materials.ResolveAsync(outputs.Single(static item => item.Kind == AssetKind.Material).IdentityUri, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = material!.MaterialUri.Should().NotBe(source.IdentityUri);
        _ = content.Items.Should().NotContain(row => row.Item.Uri == material.MaterialUri || row.Item.Uri == source.IdentityUri);
    });

    /// <summary>The existing Cook menu offers source navigation and reimport for a selected imported output.</summary>
    /// <returns>The asynchronous rendered action regression.</returns>
    [TestMethod]
    public Task ImportedOutputCommandsUseTheRetainedSourceAndSelectedInspection() => EnqueueAsync(async () =>
    {
        var source = CreateImportedSourceRow(configured: true);
        var output = CreateImportedOutputRows(source).Single(static row => row.Kind == AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([source, output]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        var messenger = new StrongReferenceMessenger();
        Uri? revealed = null;
        Uri? inspected = null;
        messenger.Register<ShowAssetRequestMessage>(this, (_, request) =>
        {
            revealed = request.AssetUri;
            request.Reply(Task.FromResult(true));
        });
        messenger.Register<OpenCookedInspectionRequestMessage>(this, (_, request) =>
        {
            inspected = request.AssetUri;
            request.Reply(Task.FromResult(true));
        });
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.ReimportSourceAsync(source.IdentityUri, projects.ActiveProject!, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        using var layout = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        var layoutView = new TilesLayoutView { ViewModel = layout };
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        using var browser = CreateBuiltinBrowserModel(provider.Object, projects, state, layout, layoutView, messenger, pipeline.Object);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        var view = new AssetsView { ViewModel = browser };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        layout.SelectedAsset = output;
        await WaitForRenderAsync().ConfigureAwait(true);
        await InvokeImportMenuActionAsync(view, "Show source", () => browser.ShowImportSourceCommand.ExecutionTask).ConfigureAwait(true);
        _ = revealed.Should().Be(source.IdentityUri);
        await InvokeImportMenuActionAsync(view, "Reimport source", () => browser.ReimportSelectedSourceCommand.ExecutionTask).ConfigureAwait(true);
        pipeline.Verify(value => value.ReimportSourceAsync(source.IdentityUri, projects.ActiveProject!, It.IsAny<CancellationToken>()), Times.Once);
        await browser.InspectCookedOutputCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = inspected.Should().Be(output.IdentityUri);
    });

    private static async Task InvokeImportMenuActionAsync(AssetsView view, string text, Func<Task?> execution)
    {
        var button = view.FindDescendant<ToolBarButton>(item => string.Equals(item.Label, "Cook", StringComparison.Ordinal))!;
        var menu = (MenuFlyout)button.Flyout;
        menu.ShowAt(button);
        await WaitForRenderAsync().ConfigureAwait(true);
        var item = menu.Items.OfType<MenuFlyoutItem>().Single(item => string.Equals(item.Text, text, StringComparison.Ordinal));
        _ = item.Visibility.Should().Be(Visibility.Visible);
        ((IInvokeProvider)new MenuFlyoutItemAutomationPeer(item).GetPattern(PatternInterface.Invoke)).Invoke();
        await execution()!.ConfigureAwait(true);
    }

    private static ContentBrowserAssetItem CreateImportedSourceRow(bool configured)
    {
        var uri = new Uri("asset:///Content/SourceMedia/DCC/Crate/model.gltf");
        return new(
            uri,
            "model.gltf",
            AssetKind.ForeignSource,
            AssetState.Source,
            DerivedState: null,
            AssetRuntimeAvailability.NotApplicable,
            uri.AbsolutePath,
            "C:/Project/Content/SourceMedia/DCC/Crate/model.gltf",
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            ImportSourceUri = configured ? uri : null,
            CookStatus = configured ? new(
            uri,
            AssetCookFreshness.Current,
            HasPublishedOutput: true,
            HasVerifiedOutput: true,
            [
                new(uri, new("asset:///Content/Models/Crate/Geometry/Main.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Models/Crate/Geometry/Main.ogeo"),
                new(uri, new("asset:///Content/Models/Crate/Materials/Paint.omat"), ContentCookAssetKind.Material, "Content", "/Content/Models/Crate/Materials/Paint.omat"),
                new(uri, new("asset:///Content/Models/Crate/Scenes/Crate.oscene"), ContentCookAssetKind.Scene, "Content", "/Content/Models/Crate/Scenes/Crate.oscene"),
            ],
            [],
            []) : null,
        };
    }

    private static ContentBrowserAssetItem[] CreateImportedOutputRows(ContentBrowserAssetItem source) => source.CookStatus!.Outputs.Select(output => new ContentBrowserAssetItem(
        output.CookedAssetUri,
        Path.GetFileNameWithoutExtension(output.VirtualPath),
        output.Kind switch { ContentCookAssetKind.Geometry => AssetKind.Geometry, ContentCookAssetKind.Material => AssetKind.Material, _ => AssetKind.Scene },
        AssetState.Cooked,
        DerivedState: null,
        AssetRuntimeAvailability.NotMounted,
        output.VirtualPath,
        SourcePath: null,
        DescriptorPath: null,
        output.CookedAssetUri,
        CookedPath: null,
        AssetGuid: null,
        [],
        IsSelectable: true)
    {
        ImportSourceUri = source.IdentityUri,
        CookStatus = source.CookStatus with { AssetUri = output.CookedAssetUri },
    }).ToArray();
}
