// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks edits of how a scene starts when played.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly Uri GameplayContext = new("asset:///Content/Input/Gameplay.oimap");
    private static readonly Uri MenuContext = new("asset:///Content/Input/Menu.oimap");

    [TestMethod]
    public async Task EditScenePlayAsync_UndoRedoRestoresTheWholePlaySettings()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var camera = CameraNode(scene, fieldOfView: 60f);
        var context = CreateContext(scene);
        var play = new ScenePlayData
        {
            StartingCamera = camera.Id,
            StartingInputContexts = [new StartingInputContextData(GameplayContext, Priority: 4), new StartingInputContextData(MenuContext)],
        };

        var result = await fixture.Sut.EditScenePlayAsync(context, play).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = ScenePlayData.AreEquivalent(scene.Play, play).Should().BeTrue();
        _ = context.IsDirty().Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Play.IsEmpty.Should().BeTrue();
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = ScenePlayData.AreEquivalent(scene.Play, play).Should().BeTrue();
    }

    [TestMethod]
    public async Task EditScenePlayAsync_RejectsANodeWithoutACameraAndRepeatedContexts()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var ground = new SceneNode(scene) { Name = "Ground" };
        scene.RootNodes.Add(ground);
        var context = CreateContext(scene);

        var notACamera = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData { StartingCamera = ground.Id }).ConfigureAwait(false);
        var repeated = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData
        {
            StartingInputContexts = [new StartingInputContextData(GameplayContext), new StartingInputContextData(GameplayContext, Priority: 2)],
        }).ConfigureAwait(false);
        var notAContext = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData
        {
            StartingInputContexts = [new StartingInputContextData(new Uri("asset:///Content/Input/Jump.oiact"))],
        }).ConfigureAwait(false);

        foreach (var result in new[] { notACamera, repeated, notAContext })
        {
            _ = result.Succeeded.Should().BeFalse();
            _ = result.ValidationCode.Should().Be("SCENE_PLAY_INVALID");
        }

        _ = scene.Play.IsEmpty.Should().BeTrue();
        _ = context.IsDirty().Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    /// <summary>Undo restores a context captured before a rename at the context's current identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoResolvesStartingInputContextRenamedAfterTheEdit()
    {
        var renamed = new Uri("asset:///Content/Controls/Gameplay.oimap");
        var fixture = CreateFixture(redirects: new FakeRedirects { Renames = { [GameplayContext] = renamed } });
        var scene = CreateScene();
        var context = CreateContext(scene);
        _ = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData { StartingInputContexts = [new StartingInputContextData(GameplayContext)] }).ConfigureAwait(false);
        _ = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData { StartingInputContexts = [new StartingInputContextData(MenuContext)] }).ConfigureAwait(false);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.Play.StartingInputContexts.Should().Equal(new StartingInputContextData(renamed));
    }
}
