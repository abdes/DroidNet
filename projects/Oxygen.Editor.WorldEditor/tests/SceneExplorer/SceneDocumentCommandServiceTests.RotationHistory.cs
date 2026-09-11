// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RotationHistoryAtGimbalLockRestoresTheWholeOrientation(bool gesture)
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Rotated" };
        scene.RootNodes.Add(node);
        var transform = node.Components.OfType<TransformComponent>().Single();
        var original = TransformConverter.EulerDegreesToQuaternion(new Vector3(90, 25, 0));
        transform.LocalRotation = original;
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var edit = PropertyEdit.Single(SceneDocumentCommandService.Transform.RotationZ, 15f);
        var token = gesture ? EditSessionToken.Begin("Rotation", [node.Id], "Z") : EditSessionToken.OneShot;
        _ = await fixture.Sut.EditPropertiesAsync(context, [node.Id], edit, "Rotate", token).ConfigureAwait(false);
        if (gesture)
        {
            token.Commit();
            _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, new Dictionary<Guid, PropertyEdit> { [node.Id] = PropertyEdit.Empty }, "Rotate", token).ConfigureAwait(false);
        }

        var edited = transform.LocalRotation;
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = MathF.Abs(Quaternion.Dot(original, edited)).Should().BeLessThan(0.999f);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MathF.Abs(Quaternion.Dot(original, transform.LocalRotation)).Should().BeApproximately(1f, 0.000001f);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MathF.Abs(Quaternion.Dot(edited, transform.LocalRotation)).Should().BeApproximately(1f, 0.000001f);
    }
}
