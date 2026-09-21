// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    public Task NodeObservationReadsPropertiesGeometryAndDeletion()
        => this.RunNativeCommandsAsync(this.CheckNativeNodeAsync);

    private static ImmutableArray<RuntimePropertyValue> ObservedNodeProperties() =>
    [
        new(1, 0, 1f),
        new(1, 1, 2f),
        new(1, 2, 3f),
        new(1, 3, 10f),
        new(1, 4, 20f),
        new(1, 5, 30f),
        new(1, 6, 2f),
        new(1, 7, 3f),
        new(1, 8, 4f),
        new(2, 0, 1.2f),
        new(2, 1, 1.6f),
        new(2, 2, 0.3f),
        new(2, 3, 2000f),
        new(2, 4, 8f),
        new(2, 5, 60f),
        new(2, 6, 200f),
        new(3, 0, 0.2f),
        new(3, 1, 0.3f),
        new(3, 2, 0.4f),
        new(3, 3, 1f),
        new(3, 4, 2f),
        new(3, 5, 0f),
        new(3, 6, 0.02f),
        new(3, 7, 0.03f),
        new(3, 8, 1f),
        new(3, 9, 1f),
        new(3, 10, 1.5f),
        new(3, 11, 80000f),
        new(3, 12, 0.04f),
        new(3, 13, 1f),
        new(3, 14, 1f),
        new(3, 15, 4f),
        new(3, 16, 0f),
        new(3, 17, 1000f),
        new(3, 18, 50f),
        new(3, 19, 150f),
        new(3, 20, 400f),
        new(3, 21, 1000f),
        new(3, 22, 2f),
        new(3, 23, 0.15f),
        new(3, 24, 0.2f),
    ];

    private async Task CheckNativeNodeAsync(RuntimeCommandDispatcher commands)
    {
        var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = (await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Node observation", this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var node = Guid.NewGuid();
        _ = (await commands.CreateNodeAsync(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeCreateNode("Observed", node, ParentId: null, InitializeWorldAsRoot: true)), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        void Send(RuntimeWorldCommand command)
            => _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, command), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();

        Send(new RuntimeAttachPerspectiveCamera(node, 1f, 1.5f, 0.1f, 1000f));
        Send(new RuntimeAttachDirectionalLight(node, 1000f, 0.01f, Vector3.One, AffectsWorld: true, Mobility: 2, CastsShadows: true, ShadowBias: 0, ShadowNormalBias: 0, ContactShadows: false, ShadowResolutionHint: 0, ExposureCompensation: 0, EnvironmentContribution: true, IsSunLight: true, CascadeCount: 4, SplitMode: 0, MaxShadowDistance: 1000f, CascadeDistances: new Vector4(50, 150, 400, 1000), DistributionExponent: 2, TransitionFraction: 0.1f, DistanceFadeoutFraction: 0.1f));
        var expected = ObservedNodeProperties();
        Send(new RuntimeSetProperties(node, expected));
        Send(new RuntimeSetGeometry(node, "asset:///Engine/Generated/BasicShapes/Cube"));
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
            _ = actual.Value.Should().BeApproximately(property.Value, 0.0001f, "component {0}, field {1}", property.ComponentId, property.FieldId);
        }

        _ = observed.State.GeometryName.Should().Be("cube");
        _ = observed.State.VertexCount.Should().BePositive();
        _ = observed.State.IndexCount.Should().Be(36);
        _ = observed.State.MaterialKeys.Should().ContainSingle();
        Send(new RuntimeRemoveSceneNodes([node]));
        var missing = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = missing.Outcome.Succeeded.Should().BeTrue();
        _ = missing.State!.Exists.Should().BeFalse();
        _ = missing.State.Properties.Should().BeEmpty();
    }
}
