// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    [DataRow(20f, 30f, 40f)]
    [DataRow(100f, 10f, 20f)]
    [DataRow(-100f, 30f, -20f)]
    [DataRow(90f, 25f, 0f)]
    public Task NodeRotationUsesEditorYxzCoordinates(float pitch, float yaw, float roll)
        => this.RunNativeCommandsAsync(async commands =>
        {
            var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
            _ = (await commands.ActivateSceneAsync(Guid.NewGuid(), target, "YXZ rotation", this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
            var node = Guid.NewGuid();
            _ = (await commands.CreateNodeAsync(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeCreateNode("Rotated", node, ParentId: null, InitializeWorldAsRoot: true)), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
            var rotation = Quaternion.CreateFromYawPitchRoll(yaw * MathF.PI / 180f, pitch * MathF.PI / 180f, roll * MathF.PI / 180f);
            _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetLocalTransform(node, Vector3.Zero, rotation, Vector3.One)), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
            var before = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertRotation(before, pitch, yaw, roll);

            _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetProperties(node, [new(1, 3, pitch + 5f)])), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
            var after = await commands.ObserveNodeAsync(Guid.NewGuid(), target, node, this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertRotation(after, pitch + 5f, yaw, roll);
        });

    private static void AssertRotation(RuntimeNodeObservation observed, float pitch, float yaw, float roll)
    {
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.Properties.Single(value => value.ComponentId == 1 && value.FieldId == 3).Value.Should().BeApproximately(pitch, 0.001f);
        _ = observed.State.Properties.Single(value => value.ComponentId == 1 && value.FieldId == 4).Value.Should().BeApproximately(yaw, 0.001f);
        _ = observed.State.Properties.Single(value => value.ComponentId == 1 && value.FieldId == 5).Value.Should().BeApproximately(roll, 0.001f);
    }
}
