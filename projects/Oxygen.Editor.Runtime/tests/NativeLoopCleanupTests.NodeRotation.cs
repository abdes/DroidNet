// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    [DataRow(20f, 30f, 40f)]
    [DataRow(10f, 100f, 20f)]
    [DataRow(-20f, -100f, 30f)]
    [DataRow(0f, 90f, 25f)]
    public Task NodeRotationUsesXyzEulerAngles(float x, float y, float z)
        => this.RunNativeCommandsAsync(async commands =>
        {
            var target = new RuntimeSceneTarget(commands.RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid());
            _ = (await commands.ActivateSceneAsync(Guid.NewGuid(), target, "XYZ rotation", this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
            var node = NodeId.New();
            _ = (await commands.CreateNodeAsync(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeCreateNode("Rotated", node, ParentId: null, InitializeWorldAsRoot: true)), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
            const float DegToRad = MathF.PI / 180f;
            var rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, z * DegToRad)
                * Quaternion.CreateFromAxisAngle(Vector3.UnitY, y * DegToRad)
                * Quaternion.CreateFromAxisAngle(Vector3.UnitX, x * DegToRad);
            _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetLocalTransform(node, Vector3.Zero, rotation, Vector3.One)), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
            var before = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertRotation(before, x, y, z);

            _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetProperties(node, [new(RuntimeComponentId.Transform, 4, new RuntimeFieldValue.Scalar(y + 5f))])), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
            var after = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertRotation(after, x, y + 5f, z);
        });

    private static void AssertRotation(RuntimeNodeObservation observed, float x, float y, float z)
    {
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.Properties.Single(value => value.ComponentId == RuntimeComponentId.Transform && value.FieldId == 3).Value.Should().BeOfType<RuntimeFieldValue.Scalar>().Which.Value.Should().BeApproximately(x, 0.001f);
        _ = observed.State.Properties.Single(value => value.ComponentId == RuntimeComponentId.Transform && value.FieldId == 4).Value.Should().BeOfType<RuntimeFieldValue.Scalar>().Which.Value.Should().BeApproximately(y, 0.001f);
        _ = observed.State.Properties.Single(value => value.ComponentId == RuntimeComponentId.Transform && value.FieldId == 5).Value.Should().BeOfType<RuntimeFieldValue.Scalar>().Which.Value.Should().BeApproximately(z, 0.001f);
    }
}
