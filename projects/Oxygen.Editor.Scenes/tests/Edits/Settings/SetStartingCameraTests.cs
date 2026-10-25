// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>
/// What is specific to <see cref="SetStartingCamera"/> (scene-edits.md 4 and 3.11): the camera node the scene starts
/// viewing through. The edit has no target node. Undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetStartingCameraTests
{
    [TestMethod]
    public async Task Camera_SetsAndRaisesStartingCameraChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(NodeReference.To(SampleScene.Cam))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.StartingCamera.Should().Be(NodeReference.To(SampleScene.Cam));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.StartingCameraChanged());
    }

    [TestMethod]
    public async Task StartingNodeNotACamera_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(NodeReference.To(SampleScene.Mesh))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.StartingCameraInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task StartingCameraMissing_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(NodeReference.To(NodeId.New()))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.StartingCameraInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SameCamera_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Builder().StartingCamera(SampleScene.Cam).Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(NodeReference.To(SampleScene.Cam))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task CameraBelowAnInstance_IsAccepted()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = fixture.Node(SampleScene.B).AddComponent(new SceneInstanceComponent { Name = "Scene Instance", Source = AssetPath.Parse("/Content/Props/Crate.oscene") });
        var camera = new NodeReference(SampleScene.B, NodePath.Of(NodeId.New()));

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(camera)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.StartingCamera.Should().Be(camera);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.StartingCameraChanged());
    }

    [TestMethod]
    public async Task CameraBelowANodeThatIsNotAnInstance_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingCamera(new NodeReference(SampleScene.B, NodePath.Of(NodeId.New())))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.StartingCameraInvalid);
        SceneAssert.Unchanged(fixture, before);
    }
}
