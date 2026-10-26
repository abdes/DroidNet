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
using Oxygen.Editor.Assets.Testing;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

[TestClass]
internal sealed class GeometryStatusTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Status updates keep the focused geometry button and do not create duplicate engine choices.</summary>
    /// <returns>The asynchronous rendered-picker regression.</returns>
    [TestMethod]
    public Task GeometryPickerSharesStatusWithoutReplacingItsFocusedChoice() => EnqueueAsync(async () =>
    {
        var asset = TestAssetEntries.CookedMaterial(0) with
        {
            Identity = AssetPath.Parse("/Content/Geometry/Custom.ogeo"),
            DisplayName = "Custom",
            Kind = AssetKind.Geometry,
        };
        var copy = asset with
        {
            Identity = EngineAssets.BasicShape("Cube"),
            DisplayName = "Cube",
            PrimaryState = AssetState.Packaged,
        };
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([asset, copy]);
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var surfaces = new FakeSurfaceProvider();
        using var session = new TestSession(TestProject.Create(new MockFileSystem()).Context);
        using var model = new GeometryViewModel(CreateStatusHosting(), provider.Object, surfaces, session)
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
        var content = model.GeometryChoices.Groups.Single(group => group.Origin == AssetOrigin.Content);
        var row = content.Rows.Should().ContainSingle().Subject;
        _ = row.Choice.Asset.Should().Be(asset.Identity);
        var engine = model.GeometryChoices.Groups.Single(group => group.Origin == AssetOrigin.Engine);
        var engineRow = engine.Rows.Should().ContainSingle().Subject;
        _ = engineRow.Choice.Asset.Should().Be(copy.Identity);
        var owner = ((FrameworkElement)view.FindName("GeometryAssetButton")).FindDescendant<SplitButton>()!;
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var button = ((FrameworkElement)flyout.Content).FindDescendant<Button>(candidate => ReferenceEquals(candidate.DataContext, row))!;
            updates.OnNext([asset with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }, copy]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = content.Rows.Should().ContainSingle().Which.Should().BeSameAs(row);
            _ = button.DataContext.Should().BeSameAs(row);
            _ = button.FindDescendant<TextBlock>(label => string.Equals(label.Text, "Geometry · Queued", StringComparison.Ordinal)).Should().NotBeNull();
            _ = engine.Rows.Should().ContainSingle().Which.Should().BeSameAs(engineRow);
        }
        finally
        {
            flyout.Hide();
        }
    });
}
