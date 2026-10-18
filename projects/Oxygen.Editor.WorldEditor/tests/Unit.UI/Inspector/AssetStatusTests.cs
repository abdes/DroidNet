// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Inspector.Geometry;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class AssetStatusTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A visible material choice updates its status without replacing its focused button.</summary>
    /// <returns>The asynchronous picker-focus regression.</returns>
    [TestMethod]
    public Task MaterialStatusUpdatesPreserveTheOpenPickerChoice() => EnqueueAsync(async () =>
    {
        var asset = CreateStatusAsset(0);
        var material = asset with { Kind = Oxygen.Editor.Assets.AssetKind.Material };
        using var updates = new BehaviorSubject<IReadOnlyList<Oxygen.Editor.Assets.AssetEntry>>([material]);
        var catalog = new Mock<Oxygen.Editor.Assets.IAssetFeed>();
        _ = catalog.SetupGet(value => value.Items).Returns(updates);
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = new GeometryViewModel(CreateStatusHosting(), catalog.Object, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), Mock.Of<Oxygen.Editor.ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(), Mock.Of<Oxygen.Editor.Projects.IProjectContextService>())
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
        var owner = (SplitButton)view.FindName("MaterialSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var content = (FrameworkElement)flyout.Content;
            var button = content.FindDescendant<Button>(candidate => candidate.DataContext is MaterialPickerRow row && row.Item.Asset == asset.Identity)!;
            _ = button.Should().NotBeNull();
            var before = button.DataContext;
            updates.OnNext([material with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = button.DataContext.Should().BeSameAs(before);
            var label = button.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Material · Queued", StringComparison.Ordinal));
            _ = label.Should().NotBeNull();
            _ = ToolTipService.GetToolTip(label!).Should().Be("Material · Queued");
        }
        finally
        {
            flyout.Hide();
        }
    });
}
