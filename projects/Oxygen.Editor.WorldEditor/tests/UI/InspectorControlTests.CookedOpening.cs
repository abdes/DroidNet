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
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World.Inspection;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks source-less asset opening without creating editable source or cooking built-ins.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Cooked and built-in entries open inspection instead of silently doing nothing or selecting a same-named authored scene.</summary>
    /// <param name="kind">The asset type.</param>
    /// <param name="builtin">Whether the asset is engine-provided.</param>
    /// <returns>The asynchronous browser invocation regression.</returns>
    [TestMethod]
    [DataRow(AssetKind.Material, false)]
    [DataRow(AssetKind.Scene, false)]
    [DataRow(AssetKind.Geometry, false)]
    [DataRow(AssetKind.Texture, false)]
    [DataRow(AssetKind.Material, true)]
    [DataRow(AssetKind.Geometry, true)]
    public Task CookedAssetOpeningUsesReadOnlyInspection(AssetKind kind, bool builtin) => EnqueueAsync(async () =>
    {
        var extension = kind switch { AssetKind.Material => "omat", AssetKind.Scene => "oscene", AssetKind.Geometry => "ogeo", _ => "otex" };
        var uri = builtin ? AssetUris.BuildGeneratedUri(kind == AssetKind.Material ? "Materials/Default" : "BasicShapes/Cube") : new Uri("asset:///Library/Main." + extension);
        var item = new ContentBrowserAssetItem(uri, "Main", kind, builtin ? AssetState.Generated : AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.NotMounted, uri.AbsolutePath, SourcePath: null, DescriptorPath: null, builtin ? null : uri, CookedPath: null, AssetGuid: null, [], IsSelectable: true);
        using var items = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = CreateQueryProvider(items);
        var projects = CreateQueryProject();
        var state = new ContentBrowserState(projects);
        using var layout = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var layoutView = new ListLayoutView { ViewModel = layout };
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
        using var browser = CreateBuiltinBrowserModel(provider.Object, projects, state, layout, layoutView, messenger);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        await LoadTestContentAsync(layoutView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        layout.InvokeItemCommand.Execute(item);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = requested.Should().NotBeNull();
        _ = requested!.AssetUri.Should().Be(uri);
        _ = requested.ScopeUri.Should().Be(uri);
        _ = sourceOpens.Should().Be(0);
    });

    /// <summary>A built-in without a project copy has useful read-only information and no meaningless validation action.</summary>
    /// <param name="geometry">Whether to inspect the engine cube or default material.</param>
    /// <returns>The asynchronous built-in document regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task UncopiedBuiltinInspectionShowsEngineInformation(bool geometry) => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var uri = AssetUris.BuildGeneratedUri(geometry ? "BasicShapes/Cube" : "Materials/Default");
        var item = new ContentBrowserAssetItem(uri, geometry ? "Cube" : "Default", geometry ? AssetKind.Geometry : AssetKind.Material, AssetState.Generated, DerivedState: null, AssetRuntimeAvailability.Mounted, uri.AbsolutePath, SourcePath: null, DescriptorPath: null, CookedUri: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true);
        using var items = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = CreateQueryProvider(items);
        _ = provider.Setup(value => value.ResolveAsync(uri, It.IsAny<CancellationToken>())).ReturnsAsync(item);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        var metadata = new CookedInspectionDocumentMetadata(projects.ActiveProject!, uri, validate: false) { AssetUri = uri };
        using var model = new CookedInspectionViewModel(metadata, pipeline.Object, provider.Object, projects, _ => Task.FromResult(true));
        var view = new CookedInspectionView { ViewModel = model };
        var root = new Grid { Width = 640, Height = 420, RequestedTheme = ElementTheme.Dark };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await model.CurrentWork.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.AssetInformationVisibility.Should().Be(Visibility.Visible);
        _ = model.StatusText.Should().Be("Built-in");
        _ = model.ValidationVisibility.Should().Be(Visibility.Collapsed);
        AssertIdleInspectionCommands(view, Visibility.Collapsed);
        _ = view.FindDescendant<AssetInformationView>()!.ViewModel!.Description.Should().Contain("No separate cooking is needed");
        _ = metadata.IsDirty.Should().BeFalse();
        pipeline.VerifyNoOtherCalls();
        await this.CaptureQueryLayoutAsync(root, "uncopied-builtin-" + geometry + ".png").ConfigureAwait(true);
        var copyUri = new Uri(geometry ? "asset:///Content/Geometry/Cube.ogeo" : "asset:///Content/Materials/Default.omat");
        var copy = item with { IdentityUri = copyUri, CookedUri = copyUri, BuiltinOriginUri = uri };
        items.OnNext([item, copy]);
        var cookedKind = geometry ? ContentCookAssetKind.Geometry : ContentCookAssetKind.Material;
        var physicalRoot = Path.Combine(projects.ActiveProject!.ProjectRoot, ".cooked", "Content");
        var inspection = new CookInspectionResult(physicalRoot, Succeeded: true, SourceIdentity: Guid.NewGuid(), [new(copyUri.AbsolutePath, cookedKind)], [], []);
        var outputRoot = new CookedRootReport("Content", IsPresent: true, inspection, Validation: null, [new(copyUri, uri, [])]);
        var report = new CookedOutputReport(projects.ActiveProject.ProjectId, uri, DateTimeOffset.UtcNow, [outputRoot]);
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(uri, It.IsAny<CancellationToken>(), It.IsAny<bool>(), projects.ActiveProject)).ReturnsAsync(report);
        await model.RefreshCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ScopedAsset!.CookedCompanions.Should().ContainSingle();
        _ = model.Assets.Should().ContainSingle();
        _ = model.AssetInformationVisibility.Should().Be(Visibility.Collapsed);
        _ = model.ValidationVisibility.Should().Be(Visibility.Visible);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertIdleInspectionCommands(view, Visibility.Visible);
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
