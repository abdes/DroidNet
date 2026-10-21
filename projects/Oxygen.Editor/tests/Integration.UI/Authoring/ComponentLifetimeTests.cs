// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Interop;
using Oxygen.Managed.Core;
using static Oxygen.Editor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.Integration.UI.Tests.Authoring;

[TestClass]
public sealed partial class ComponentLifetimeTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A refused transform removal leaves the native transform, and component add/remove history and saved reload keep the native component set.</summary>
    /// <param name="camera">Whether to exercise a camera or a directional light.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task ComponentTransitionsAndLockedTransformReachNativeState(bool camera) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, "Transform"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        await AssertTransformRemovalRefusedAsync(fixture, timeout.Token).ConfigureAwait(true);
        var type = camera ? typeof(PerspectiveCamera) : typeof(DirectionalLightComponent);
        var added = await fixture.Authoring.ExecuteAsync(SceneComponents.Add(fixture.Authoring, node.Id, camera ? SceneComponents.PerspectiveCamera : SceneComponents.DirectionalLight)).ConfigureAwait(true);
        _ = added.Succeeded.Should().BeTrue();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        var reopened = fixture.Source.RootNodes.Single();
        var component = reopened.Components.Single(value => value.GetType() == type);
        _ = (await fixture.Authoring.ExecuteAsync(new RemoveComponent(reopened.Id, component.Id)).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.Document.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
    });

    private static async Task AssertTransformRemovalRefusedAsync(NativeSceneFixture fixture, CancellationToken cancellationToken)
    {
        var node = fixture.Source.RootNodes.Single();
        var transform = node.Components.OfType<TransformComponent>().Single();
        _ = (await fixture.Authoring.ExecuteAsync(new RemoveComponent(node.Id, transform.Id)).ConfigureAwait(true)).Succeeded.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        var native = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
        _ = native.Properties.Where(static value => value.ComponentId == RuntimeComponentId.Transform).Select(static value => value.FieldId)
            .Should().BeEquivalentTo(Enum.GetValues<TransformField>().Select(static field => (ushort)field), "the refused removal must leave the native transform intact");
    }

    private static async Task AssertComponentPresenceAsync(NativeSceneFixture fixture, NodeId nodeId, bool camera, bool present, CancellationToken cancellationToken)
    {
        var componentId = camera ? RuntimeComponentId.PerspectiveCamera : RuntimeComponentId.DirectionalLight;
        var native = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        var fields = native.Properties.Where(value => value.ComponentId == componentId).Select(static value => value.FieldId);
        if (present)
        {
            var expected = camera ? Enum.GetValues<PerspectiveCameraField>().Select(static field => (ushort)field) : Enum.GetValues<DirectionalLightField>().Select(static field => (ushort)field);
            _ = fields.Should().BeEquivalentTo(expected);
        }
        else
        {
            _ = fields.Should().BeEmpty();
        }

        _ = fixture.Source.RootNodes.Single(node => node.Id == nodeId).Components.Any(component => camera ? component is PerspectiveCamera : component is DirectionalLightComponent).Should().Be(present);
    }
}
