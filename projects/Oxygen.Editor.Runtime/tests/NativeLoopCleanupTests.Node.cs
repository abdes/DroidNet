// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    // The wire values of the local source modes of a node flag.
    private const int LocalOnValue = 1;
    private const int LocalOffValue = 2;

    private static readonly RuntimeFieldValue LocalOnMode = Integer(LocalOnValue);
    private static readonly RuntimeFieldValue LocalOffMode = Integer(LocalOffValue);

    [TestMethod]
    public Task NodeObservationReadsPropertiesGeometryAndDeletion()
        => this.RunNativeCommandsAsync(this.CheckNativeNodeAsync);

    private static ImmutableArray<RuntimePropertyValue> ObservedNodeProperties() =>
    [
        new(RuntimeComponentId.Transform, 0, Scalar(1f)),
        new(RuntimeComponentId.Transform, 1, Scalar(2f)),
        new(RuntimeComponentId.Transform, 2, Scalar(3f)),
        new(RuntimeComponentId.Transform, 3, Scalar(10f)),
        new(RuntimeComponentId.Transform, 4, Scalar(20f)),
        new(RuntimeComponentId.Transform, 5, Scalar(30f)),
        new(RuntimeComponentId.Transform, 6, Scalar(2f)),
        new(RuntimeComponentId.Transform, 7, Scalar(3f)),
        new(RuntimeComponentId.Transform, 8, Scalar(4f)),
        new(RuntimeComponentId.PerspectiveCamera, 0, Scalar(1.2f)),
        new(RuntimeComponentId.PerspectiveCamera, 1, Scalar(1.6f)),
        new(RuntimeComponentId.PerspectiveCamera, 2, Scalar(0.3f)),
        new(RuntimeComponentId.PerspectiveCamera, 3, Scalar(2000f)),
        new(RuntimeComponentId.PerspectiveCamera, 4, Scalar(8f)),
        new(RuntimeComponentId.PerspectiveCamera, 5, Scalar(60f)),
        new(RuntimeComponentId.PerspectiveCamera, 6, Scalar(200f)),
        new(RuntimeComponentId.PerspectiveCamera, 7, Integer((int)Oxygen.Managed.Core.CameraAspectMode.Auto)),
        new(RuntimeComponentId.DirectionalLight, 0, Scalar(0.2f)),
        new(RuntimeComponentId.DirectionalLight, 1, Scalar(0.3f)),
        new(RuntimeComponentId.DirectionalLight, 2, Scalar(0.4f)),
        new(RuntimeComponentId.DirectionalLight, 3, Flag(true)),
        new(RuntimeComponentId.DirectionalLight, 5, Flag(false)),
        new(RuntimeComponentId.DirectionalLight, 6, Scalar(0.02f)),
        new(RuntimeComponentId.DirectionalLight, 7, Scalar(0.03f)),
        new(RuntimeComponentId.DirectionalLight, 8, Flag(true)),
        new(RuntimeComponentId.DirectionalLight, 9, Integer(1)),
        new(RuntimeComponentId.DirectionalLight, 10, Scalar(1.5f)),
        new(RuntimeComponentId.DirectionalLight, 11, Scalar(80000f)),
        new(RuntimeComponentId.DirectionalLight, 12, Scalar(0.04f)),
        new(RuntimeComponentId.DirectionalLight, 15, Integer(4)),
        new(RuntimeComponentId.DirectionalLight, 16, Integer(0)),
        new(RuntimeComponentId.DirectionalLight, 17, Scalar(1000f)),
        new(RuntimeComponentId.DirectionalLight, 18, Scalar(50f)),
        new(RuntimeComponentId.DirectionalLight, 19, Scalar(150f)),
        new(RuntimeComponentId.DirectionalLight, 20, Scalar(400f)),
        new(RuntimeComponentId.DirectionalLight, 21, Scalar(1000f)),
        new(RuntimeComponentId.DirectionalLight, 22, Scalar(2f)),
        new(RuntimeComponentId.DirectionalLight, 23, Scalar(0.15f)),
        new(RuntimeComponentId.DirectionalLight, 24, Scalar(0.2f)),
        new(RuntimeComponentId.DirectionalLight, 25, Integer(1)),
        new(RuntimeComponentId.DirectionalLight, 26, Flag(true)),
        new(RuntimeComponentId.DirectionalLight, 27, Scalar(0.5f)),
        new(RuntimeComponentId.DirectionalLight, 28, Scalar(1.25f)),
        new(RuntimeComponentId.DirectionalLight, 29, Scalar(2f)),
        // Visibility and the shadow flags take a source mode (Inherit 0, Shown/On 1, Hidden/Off 2).
        new(RuntimeComponentId.Node, (ushort)NodeField.Visible, LocalOnMode),
        new(RuntimeComponentId.Node, (ushort)NodeField.CastsShadows, LocalOffMode),
        new(RuntimeComponentId.Node, (ushort)NodeField.ReceivesShadows, LocalOnMode),

        // A node created natively keeps the engine's creation defaults for the flags the test never sets.
        new(RuntimeComponentId.Node, 3, Flag(false)),
        new(RuntimeComponentId.Node, 4, Flag(false)),
        new(RuntimeComponentId.Node, 5, Flag(false)),
    ];

    private static RuntimeFieldValue Scalar(float value) => new RuntimeFieldValue.Scalar(value);

    private static RuntimeFieldValue Integer(int value) => new RuntimeFieldValue.Integer(value);

    private static RuntimeFieldValue Flag(bool value) => new RuntimeFieldValue.Flag(value);

    // A node flag with a source mode is observed as its effective bool; the others as the value sent.
    private static RuntimeFieldValue ObservedValue(RuntimePropertyValue sent)
        => sent.ComponentId == RuntimeComponentId.Node
            && (NodeField)sent.FieldId is NodeField.Visible or NodeField.CastsShadows or NodeField.ReceivesShadows
                ? sent.Value switch
                {
                    RuntimeFieldValue.Integer { Value: LocalOnValue } => Flag(true),
                    RuntimeFieldValue.Integer { Value: LocalOffValue } => Flag(false),
                    _ => throw new ArgumentOutOfRangeException(nameof(sent), "The test sends local modes only."),
                }
                : sent.Value;

    private static void AssertObserved(RuntimeFieldValue actual, RuntimeFieldValue expected, RuntimePropertyValue property)
    {
        if (expected is RuntimeFieldValue.Scalar scalar)
        {
            _ = actual.Should().BeOfType<RuntimeFieldValue.Scalar>("component {0}, field {1}", property.ComponentId, property.FieldId)
                .Which.Value.Should().BeApproximately(scalar.Value, 0.0001f, "component {0}, field {1}", property.ComponentId, property.FieldId);
        }
        else
        {
            _ = actual.Should().Be(expected, "component {0}, field {1}", property.ComponentId, property.FieldId);
        }
    }

    private async Task CheckNativeNodeAsync(RuntimeCommandDispatcher commands)
    {
        var target = new RuntimeSceneTarget(commands.RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid());
        _ = (await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Node observation", this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var node = NodeId.New();
        _ = (await commands.CreateNodeAsync(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeCreateNode("Observed", node, ParentId: null, InitializeWorldAsRoot: true)), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        void Send(RuntimeWorldCommand command)
            => _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, command), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();

        Send(new RuntimeAttachPerspectiveCamera(node, 1f, 1.5f, 0.1f, 1000f, Oxygen.Managed.Core.CameraAspectMode.Auto));
        Send(new RuntimeAttachDirectionalLight(node, 1000f, 0.01f, Vector3.One, AffectsWorld: true, Mobility: 0, CastsShadows: true, ShadowBias: 0, ShadowNormalBias: 0, ContactShadows: false, ShadowResolutionHint: 0, ExposureCompensation: 0, AtmosphereLightSlot: 1, UsePerPixelAtmosphereTransmittance: true, AtmosphereDiskLuminanceScaleRgb: Vector3.One, CascadeCount: 4, SplitMode: 0, MaxShadowDistance: 1000f, CascadeDistances: new Vector4(50, 150, 400, 1000), DistributionExponent: 2, TransitionFraction: 0.1f, DistanceFadeoutFraction: 0.1f));
        var expected = ObservedNodeProperties();
        Send(new RuntimeSetProperties(node, expected));
        Send(new RuntimeSetGeometry(node, EngineAssets.BasicShape("Cube").VirtualPath));
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        RuntimeNodeObservation observed;
        do
        {
            observed = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, timeout.Token).ConfigureAwait(false);
            _ = observed.Outcome.Succeeded.Should().BeTrue();
            _ = observed.State.Should().NotBeNull();
            if (observed.State!.GeometryName.Length == 0)
            {
                await Task.Delay(20, timeout.Token).ConfigureAwait(false);
            }
        }
        while (observed.State!.GeometryName.Length == 0);

        _ = observed.NodeId.Should().Be(node);
        _ = observed.State.Exists.Should().BeTrue();
        _ = observed.State.IsPrimarySun.Should().BeTrue();
        _ = observed.State.Properties.Should().HaveCount(expected.Length);
        foreach (var property in expected)
        {
            var actual = observed.State.Properties.Single(value => value.ComponentId == property.ComponentId && value.FieldId == property.FieldId);
            AssertObserved(actual.Value, ObservedValue(property), property);
        }

        _ = observed.State.GeometryName.Should().Be("Cube");
        _ = observed.State.VertexCount.Should().BePositive();
        _ = observed.State.IndexCount.Should().Be(36);
        _ = observed.State.MaterialKeys.Should().ContainSingle();
        Send(new RuntimeSetProperties(node, [new(RuntimeComponentId.PerspectiveCamera, 7, Integer((int)Oxygen.Managed.Core.CameraAspectMode.Fixed))]));
        var fixedCamera = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, timeout.Token).ConfigureAwait(false);
        _ = fixedCamera.Outcome.Succeeded.Should().BeTrue();
        _ = fixedCamera.State!.Properties.Single(value => value.ComponentId == RuntimeComponentId.PerspectiveCamera && value.FieldId == 7)
            .Value.Should().Be(Integer((int)Oxygen.Managed.Core.CameraAspectMode.Fixed));
        _ = fixedCamera.State.Properties.Single(value => value.ComponentId == RuntimeComponentId.PerspectiveCamera && value.FieldId == 1)
            .Value.Should().Be(Scalar(1.6f));
        Send(new RuntimeRemoveSceneNodes([node]));
        var missing = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = missing.Outcome.Succeeded.Should().BeTrue();
        _ = missing.State!.Exists.Should().BeFalse();
        _ = missing.State.Properties.Should().BeEmpty();
    }
}
