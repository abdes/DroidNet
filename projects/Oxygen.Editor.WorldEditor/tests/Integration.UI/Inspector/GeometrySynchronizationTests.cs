// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Inspector;

[TestClass]
public sealed partial class GeometrySynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Every engine built-in resolves with drawable buffers and retains its URI across Save/reopen.</summary>
    /// <param name="shape">The canonical built-in generator name.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Cube")]
    [DataRow("SubdividedCube")]
    [DataRow("Sphere")]
    [DataRow("IcoSphere")]
    [DataRow("Capsule")]
    [DataRow("Plane")]
    [DataRow("Cylinder")]
    [DataRow("Cone")]
    [DataRow("Torus")]
    [DataRow("Quad")]
    public Task BuiltinGeometryResolvesAndReopensThroughNativeRuntime(string shape) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, shape));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var nodeId = fixture.Source.RootNodes.Single().Id;
        var original = await AssertGeometryAsync(fixture, nodeId, shape, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        var reopened = await AssertGeometryAsync(fixture, nodeId, shape, timeout.Token).ConfigureAwait(true);
        _ = reopened.GeometryKey.Should().Be(original.GeometryKey);
        _ = reopened.VertexCount.Should().Be(original.VertexCount);
        _ = reopened.IndexCount.Should().Be(original.IndexCount);
    });

    /// <summary>A mixed geometry selection converges through the picker, history and saved source.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task GeometryPickerMixedSelectionHistoryAndReopenReachNativeState() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            AddGeometryNode(scene, "Cube");
            AddGeometryNode(scene, "Plane");
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var nodes = fixture.Source.RootNodes.Select(node => node.Id).ToArray();
        using var host = fixture.CreateInspectorHost(fixture.Source.RootNodes.ToList());
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView
        {
            ViewModel = model,
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var button = (SplitButton)await FindInspectorControlAsync(scroller, () => view.FindDescendant<SplitButton>(value => string.Equals(value.Name, "AssetSplitButton", StringComparison.Ordinal)), "Geometry", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await PickAssetAsync(button, "Sphere", material: false, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("Sphere");
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Cube", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Plane", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
    });
}
