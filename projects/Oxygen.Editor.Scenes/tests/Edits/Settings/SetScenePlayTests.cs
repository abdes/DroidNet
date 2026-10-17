// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>
/// What is specific to <see cref="SetScenePlay"/> (scene-edits.md 4 and 3.11): the scene's starting camera and input
/// contexts are replaced as a whole. The edit has no target node. Undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetScenePlayTests
{
    [TestMethod]
    public async Task CameraAndContexts_SetAndRaiseScenePlayChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var play = new ScenePlayData { StartingCamera = SampleScene.Cam, StartingInputContexts = [new StartingInputContextData(SampleScene.PlayInput, 3)] };

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(play)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.Play.StartingCamera.Should().Be(SampleScene.Cam);
        _ = fixture.Scene.Play.StartingInputContexts.Should().Equal(new StartingInputContextData(SampleScene.PlayInput, 3));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScenePlayChanged());
    }

    [TestMethod]
    public async Task StartingNodeNotACamera_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(new ScenePlayData { StartingCamera = SampleScene.Mesh })).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PlayCameraInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task StartingCameraMissing_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(new ScenePlayData { StartingCamera = NodeId.New() })).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PlayCameraInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task RepeatedInputContext_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var play = new ScenePlayData { StartingInputContexts = [new StartingInputContextData(SampleScene.PlayInput, 1), new StartingInputContextData(SampleScene.PlayInput, 2)] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(play)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryDuplicate);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NullContextEntry_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var play = new ScenePlayData { StartingInputContexts = [null!] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(play)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ContextNotAnInputAsset_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var play = new ScenePlayData { StartingInputContexts = [new StartingInputContextData(SampleScene.MoveScript)] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(play)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task EquivalentSettings_AppliesWithoutAnEntry()
    {
        var current = new ScenePlayData { StartingCamera = SampleScene.Cam, StartingInputContexts = [new StartingInputContextData(SampleScene.PlayInput, 3)] };
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Builder().Play(current).Build()).ConfigureAwait(false);
        var equivalent = new ScenePlayData { StartingCamera = SampleScene.Cam, StartingInputContexts = [new StartingInputContextData(SampleScene.PlayInput, 3)] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScenePlay(equivalent)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }
}
