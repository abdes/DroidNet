// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Catalog;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class BuiltinCatalogTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The ten authoring choices remain available offline, with advanced metadata and a working retry.</summary>
    /// <returns>The asynchronous picker-availability regression.</returns>
    [TestMethod]
    public Task BuiltinPickerShowsAllNativeNamesAndRecoversItsCatalog() => EnqueueAsync(async () =>
    {
        var discovery = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var live = discovery.Snapshot;
        discovery.SetSnapshot(new(live.Catalog, IsLastKnown: true, "Using the last-known engine catalog. Preview unavailable."));
        var catalog = new Mock<Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider>();
        _ = catalog.SetupGet(value => value.Items).Returns(Observable.Empty<IReadOnlyList<Oxygen.Editor.ContentBrowser.AssetIdentity.ContentBrowserAssetItem>>());
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<Oxygen.Editor.ContentBrowser.AssetIdentity.AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var materials = new Mock<IMaterialPickerService>();
        _ = materials.SetupGet(value => value.Results).Returns(Observable.Return<IReadOnlyList<MaterialPickerResult>>([]));
        _ = materials.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = new GeometryViewModel(CreateStatusHosting(), catalog.Object, materials.Object, discovery, Mock.Of<Oxygen.Editor.World.Services.ISceneContentDemandService>(), Mock.Of<Oxygen.Editor.ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(), Mock.Of<Oxygen.Editor.Projects.IProjectContextService>())
        {
            IsExpanded = true
        };
        var view = new GeometryView
        {
            ViewModel = model,
            Width = 440
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var engine = model.Groups.Single(group => string.Equals(group.Key, "Engine", StringComparison.Ordinal));
        _ = engine.Items.Should().Equal(live.Catalog!.AuthoringGeometries, (item, definition) => item.Item.Uri == definition.AssetUri);
        _ = engine.Items.Should().HaveCount(10).And.OnlyContain(item => item.Item.IsEnabled);
        _ = engine.Items.Single(item => string.Equals(item.Item.Name, "SubdividedCube", StringComparison.Ordinal)).Item.DisplayType.Should().Contain("Advanced");
        _ = engine.Items.Should().NotContain(item => string.Equals(item.Item.Name, "ArrowGizmo", StringComparison.Ordinal));
        var owner = (SplitButton)view.FindName("AssetSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var notice = ((FrameworkElement)flyout.Content).FindDescendant<InfoBar>()!;
            _ = notice.IsOpen.Should().BeTrue();
            _ = notice.Message.Should().Contain("Preview unavailable");
            discovery.RefreshResult = live;
            await model.RetryBuiltinCatalogCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = notice.IsOpen.Should().BeFalse();
            _ = engine.Items.Should().HaveCount(10).And.OnlyContain(item => !item.Item.DisplayType.Contains("Preview unavailable", StringComparison.Ordinal));
        }
        finally
        {
            flyout.Hide();
        }
    });
}
