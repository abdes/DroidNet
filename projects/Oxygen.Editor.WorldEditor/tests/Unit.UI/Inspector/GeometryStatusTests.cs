// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class GeometryStatusTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Status updates keep the focused geometry button and do not create duplicate built-in choices.</summary>
    /// <returns>The asynchronous rendered-picker regression.</returns>
    [TestMethod]
    public Task GeometryPickerSharesStatusWithoutReplacingItsFocusedChoice() => EnqueueAsync(async () =>
    {
        var asset = CreateStatusAsset(0) with
        {
            Identity = AssetPath.Parse("/Content/Geometry/Custom.ogeo"),
            DisplayName = "Custom",
            Kind = AssetKind.Geometry,
        };
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var engine = builtins.Snapshot.Catalog!.CreateCatalogRecords()[0];
        var copy = asset with
        {
            Identity = engine.Generated!.CookedVirtualPath,
            Generated = engine.Generated,
            BuiltinOrigin = engine.Identity,
            PrimaryState = AssetState.Generated,
        };
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([asset, copy]);
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = new GeometryViewModel(CreateStatusHosting(), provider.Object, builtins, Mock.Of<Oxygen.Editor.ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(), Mock.Of<Oxygen.Editor.Projects.IProjectContextService>())
        {
            IsExpanded = true,
        };
        var view = new GeometryView
        {
            ViewModel = model,
            Width = 440,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var content = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal));
        var row = content.Items.Should().ContainSingle().Subject;
        _ = row.Item.Asset.Should().Be(asset.Identity);
        var owner = (SplitButton)view.FindName("AssetSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var button = ((FrameworkElement)flyout.Content).FindDescendant<Button>(candidate => ReferenceEquals(candidate.DataContext, row))!;
            updates.OnNext([asset with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }, copy]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = content.Items.Should().ContainSingle().Which.Should().BeSameAs(row);
            _ = button.DataContext.Should().BeSameAs(row);
            _ = button.FindDescendant<TextBlock>(label => string.Equals(label.Text, "Geometry · Queued", StringComparison.Ordinal)).Should().NotBeNull();
            _ = model.Groups.Single(group => string.Equals(group.Key, "Engine", StringComparison.Ordinal)).Items.Select(choice => choice.Item.Asset).Should().BeEquivalentTo(builtins.Snapshot.Catalog!.AuthoringGeometries.Select(definition => definition.AssetUri));
        }
        finally
        {
            flyout.Hide();
        }
    });
}
