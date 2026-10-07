// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Inspector;

[TestClass]
public sealed partial class GeometrySynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly string[] BuiltinShapes = ["Cube", "SubdividedCube", "Sphere", "IcoSphere", "Capsule", "Plane", "Cylinder", "Cone", "Torus", "Quad"];

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Every engine built-in resolves with drawable buffers and retains its URI across Save/reopen.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task BuiltinGeometriesResolveAndReopenThroughNativeRuntime() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, static scene =>
        {
            foreach (var shape in BuiltinShapes)
            {
                AddGeometryNode(scene, shape);
            }
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var nodes = fixture.Source.RootNodes.Select(static node => (node.Id, Shape: node.Name)).ToArray();
        var original = new Dictionary<Guid, RuntimeNodeState>();
        foreach (var (id, shape) in nodes)
        {
            original[id] = await AssertGeometryAsync(fixture, id, shape, timeout.Token).ConfigureAwait(true);
        }

        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        foreach (var (id, shape) in nodes)
        {
            var reopened = await AssertGeometryAsync(fixture, id, shape, timeout.Token).ConfigureAwait(true);
            _ = reopened.GeometryKey.Should().Be(original[id].GeometryKey, shape);
            _ = reopened.VertexCount.Should().Be(original[id].VertexCount, shape);
            _ = reopened.IndexCount.Should().Be(original[id].IndexCount, shape);
        }
    });
}
