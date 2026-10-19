// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World.Inspection;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserControls;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspection;

[TestClass]
internal sealed class CookedOpeningTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Inspecting a masked library copy keeps that physical source and explains what assignments will use.</summary>
    /// <returns>The asynchronous physical-source report regression.</returns>
    [TestMethod]
    public Task LibraryInspectionReportsTheSelectedCopyAndEffectiveSource() => EnqueueAsync(async () =>
    {
        var directory = Directory.CreateTempSubdirectory("Oxygen-LibraryInspection-");
        try
        {
            var projects = CreateQueryProject();
            var asset = AssetPath.Parse("/Content/Shared.omat");
            await File.WriteAllBytesAsync(Path.Combine(directory.FullName, "Shared.omat"), [1], this.TestContext.CancellationToken).ConfigureAwait(true);
            var low = new CookedAssetMetadata(FileSystemPath.From(directory.FullName), "Shared.omat", Guid.CreateVersion7(), new(1, 2), 1, 1, new string('0', 64))
            {
                VirtualPath = asset,
            };
            var high = low with
            {
                ContainerPath = projects.ActiveProject!.ProjectRoot.Combine(".cooked").Combine("Content"),
                AssetKey = new(3, 4),
            };
            var effective = new AssetEntry(asset, "Shared", AssetKind.Material, AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.Mounted, asset.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: high.ContainerPath.Combine("Shared.omat"), AssetGuid: null, [], IsSelectable: true)
            {
                CookedMetadata = high,
                OverriddenCookedSources = [low],
            };
            var provider = new Mock<IAssetFeed>();
            _ = provider.Setup(value => value.ResolveAsync(asset, It.IsAny<CancellationToken>())).ReturnsAsync(effective);
            var scope = new CookedInspectionScope(projects.ActiveProject!, AssetPath.Parse("/Library/Shared.omat"), Validate: false)
            {
                Asset = asset,
                CookedSource = low,
            };
            var inspection = new CookInspectionResult(directory.FullName, Succeeded: true, low.SourceIdentity, [new(asset.VirtualPath, ContentCookAssetKind.Material)], [], []);
            var report = new CookedOutputReport(projects.ActiveProject!.ProjectId, scope.Scope, DateTimeOffset.UtcNow, [new("Library", IsPresent: true, inspection, Validation: null, [])]);
            var pipeline = new Mock<IContentPipelineService>();
            _ = pipeline.Setup(value => value.InspectCookedOutputAsync(scope.Scope, It.IsAny<CancellationToken>(), validate: false, projects.ActiveProject)).ReturnsAsync(report);
            using var model = new CookedInspectionViewModel(pipeline.Object, provider.Object, projects, _ => Task.FromResult(true));
            var view = new CookedInspectionView
            {
                ViewModel = model,
            };
            var root = new Grid
            {
                Width = 700,
                Height = 480,
                RequestedTheme = ElementTheme.Dark,
            };
            root.Children.Add(view);
            await LoadTestContentAsync(root).ConfigureAwait(true);
            await model.InspectAsync(scope).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = model.ScopedAsset!.CookedMetadata.Should().Be(low);
            _ = model.SelectedAsset!.ResolutionText.Should().Contain("project output");
            _ = model.SelectedAsset.Root.Inspection.CookedRoot.Should().Be(directory.FullName);
            _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text.Contains("project output", StringComparison.Ordinal));
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    });

    /// <summary>Cooked and engine entries open inspection instead of silently doing nothing or selecting a same-named authored scene.</summary>
    /// <param name="kind">The asset type.</param>
    /// <param name="engine">Whether the asset is packaged in the engine content pak.</param>
    /// <returns>The asynchronous browser invocation regression.</returns>
    [TestMethod]
    [DataRow(AssetKind.Material, false)]
    [DataRow(AssetKind.Scene, false)]
    [DataRow(AssetKind.Geometry, false)]
    [DataRow(AssetKind.Texture, false)]
    [DataRow(AssetKind.Material, true)]
    [DataRow(AssetKind.Geometry, true)]
    public Task CookedAssetOpeningUsesReadOnlyInspection(AssetKind kind, bool engine) => EnqueueAsync(async () =>
    {
        var extension = kind switch
        {
            AssetKind.Material => "omat",
            AssetKind.Scene => "oscene",
            AssetKind.Geometry => "ogeo",
            _ => "otex",
        };
        var uri = engine ? (kind == AssetKind.Material ? EngineAssets.DefaultMaterial : EngineAssets.BasicShape("Cube")) : AssetPath.Parse("/Library/Main." + extension);
        var item = new AssetEntry(uri, "Main", kind, engine ? AssetState.Packaged : AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.NotMounted, uri.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true);
        using var items = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        var provider = CreateQueryProvider(items);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState();
        using var layout = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var layoutView = new ListLayoutView
        {
            ViewModel = layout,
        };
        var messenger = new StrongReferenceMessenger();
        OpenCookedInspectionRequestMessage? requested = null;
        var sourceOpens = 0;
        var listener = new object();
        messenger.Register<OpenCookedInspectionRequestMessage>(listener, (_, message) =>
        {
            requested = message;
            message.Reply(Task.FromResult(true));
        });
        messenger.Register<OpenMaterialRequestMessage>(listener, (_, _) => sourceOpens++);
        messenger.Register<OpenSceneRequestMessage>(listener, (_, _) => sourceOpens++);
        using var browser = CreateBrowserModel(provider.Object, projects, state, layout, layoutView, messenger);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        await LoadTestContentAsync(layoutView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        layout.InvokeItemCommand.Execute(item);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = requested.Should().NotBeNull();
        _ = requested!.Asset.Should().Be(uri);
        _ = requested.Scope.Should().Be(uri);
        _ = sourceOpens.Should().Be(0);
    });

    /// <summary>A packaged engine asset has useful read-only information and no meaningless validation action.</summary>
    /// <param name="geometry">Whether to inspect the engine cube or default material.</param>
    /// <returns>The asynchronous packaged-asset document regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task PackagedEngineAssetInspectionShowsEngineInformation(bool geometry) => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var uri = geometry ? EngineAssets.BasicShape("Cube") : EngineAssets.DefaultMaterial;
        var item = new AssetEntry(uri, geometry ? "Cube" : "Default", geometry ? AssetKind.Geometry : AssetKind.Material, AssetState.Packaged, DerivedState: null, AssetRuntimeAvailability.Mounted, uri.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true);
        using var items = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        var provider = CreateQueryProvider(items);
        _ = provider.Setup(value => value.ResolveAsync(uri, It.IsAny<CancellationToken>())).ReturnsAsync(item);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        var scope = new CookedInspectionScope(projects.ActiveProject!, uri, Validate: false)
        {
            Asset = uri,
        };
        using var model = new CookedInspectionViewModel(pipeline.Object, provider.Object, projects, _ => Task.FromResult(true));
        var view = new CookedInspectionView
        {
            ViewModel = model,
        };
        var root = new Grid
        {
            Width = 640,
            Height = 420,
            RequestedTheme = ElementTheme.Dark,
        };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await model.InspectAsync(scope).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.AssetInformationVisibility.Should().Be(Visibility.Visible);
        _ = model.StatusText.Should().Be(item.PackagedBadge);
        _ = model.ValidationVisibility.Should().Be(Visibility.Collapsed);
        AssertIdleInspectionCommands(view, Visibility.Collapsed);
        _ = view.FindDescendant<AssetInformationView>()!.ViewModel!.Description.Should().Contain("read-only");
        pipeline.VerifyNoOtherCalls();
    });

    private static void AssertIdleInspectionCommands(CookedInspectionView view, Visibility validation)
    {
        var toolbar = view.FindDescendant<ToolBar>()!;
        var validate = toolbar.PrimaryItems.OfType<ToolBarButton>().Single(button => string.Equals(button.Label, "Validate", StringComparison.Ordinal));
        var cancel = toolbar.PrimaryItems.OfType<ToolBarButton>().Single(button => string.Equals(button.Label, "Cancel", StringComparison.Ordinal));
        _ = validate.Visibility.Should().Be(validation);
        _ = cancel.Visibility.Should().Be(Visibility.Collapsed);
    }
}
