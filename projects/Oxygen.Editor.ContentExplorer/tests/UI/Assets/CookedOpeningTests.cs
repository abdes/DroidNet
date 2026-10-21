// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerControls;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Assets;

/// <summary>Opening a cooked or engine asset inspects its cooked output.</summary>
[TestClass]
internal sealed class CookedOpeningTests : DroidNet.Tests.VisualUserInterfaceTests
{
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
        var state = new ContentExplorerState();
        using var layout = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        var layoutView = new ListLayoutView
        {
            ViewModel = layout,
        };
        CookedInspectionRequest? requested = null;
        var placement = new Mock<IPlacementService>();
        var inspections = new Mock<ICookedInspections>();
        _ = inspections.Setup(value => value.InspectAsync(It.IsAny<CookedInspectionRequest>()))
            .Callback((CookedInspectionRequest request) => requested = request)
            .ReturnsAsync(true);
        using var browser = CreateAssetsModel(provider.Object, projects, state, layout, layoutView, placement: placement.Object, cookedInspections: inspections.Object);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        browser.LoadContent(layout, "right");
        await LoadTestContentAsync(layoutView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        layout.InvokeItemCommand.Execute(item);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = requested.Should().NotBeNull();
        _ = requested!.Asset.Should().Be(uri);
        _ = requested.Scope.Should().Be(uri);
        placement.Verify(value => value.OpenDocumentAsync(It.IsAny<AssetPath>(), It.IsAny<WindowPlacement>(), It.IsAny<CancellationToken>()), Times.Never);
    });
}
