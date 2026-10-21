// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Testing;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>The geometry picker offers an imported model's geometry output, never the model itself.</summary>
[TestClass]
internal sealed class GeometryImportedOutputsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>The geometry picker begins empty for a source model, then offers only its geometry output.</summary>
    /// <returns>The asynchronous typed-picker regression.</returns>
    [TestMethod]
    public Task ImportedOutputsPopulateOnlyMatchingPickerTypes() => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([TestAssetEntries.ImportedModel(imported: false)]);
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
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
        var source = TestAssetEntries.ImportedModel(imported: true);
        var outputs = TestAssetEntries.ImportedOutputs(source);
        updates.OnNext([source, .. outputs]);
        await WaitForRenderAsync().ConfigureAwait(true);
        var content = geometry.GeometryChoices.Groups.Single(static group => group.Origin == AssetOrigin.Content);
        _ = content.Rows.Should().ContainSingle().Which.Choice.Asset.Should().Be(outputs.Single(static item => item.Kind == AssetKind.Geometry).Identity);
        var material = outputs.Single(static item => item.Kind == AssetKind.Material).Identity;
        _ = content.Rows.Should().NotContain(row => row.Choice.Asset == material || row.Choice.Asset == source.Identity);
    });
}
