// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Verifies explicit saves finish gestures and preserve conflict/copy ownership.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task SaveSceneCommitsGestureOnceAndRejectsItsLatePreview()
    {
        SceneSaveSnapshot? saved = null;
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.GetSceneSourceVersion(It.IsAny<Scene>())).Returns(value: (SceneSourceVersion?)null);
        _ = manager.Setup(value => value.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Callback<SceneSaveSnapshot>(snapshot => saved = snapshot).ReturnsAsync(value: true);
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var node = scene.RootNodes[0];
        var context = CreateContext(scene);
        ConfigureTransformSessionPropertySync(fixture, scene, node, []);
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [node.Id], "PositionX");
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(7f), session).ConfigureAwait(false);

        _ = (await fixture.Sut.SaveSceneAsync(context).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = saved.Should().NotBeNull();
        var persisted = Scene.CreateAndHydrate(scene.Project, JsonSerializer.Deserialize(saved!.Json, SceneJsonContext.Default.SceneData)!);
        _ = persisted.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(7);
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(99f), session).ConfigureAwait(false);
        session.Commit();
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(99f), session).ConfigureAwait(false);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(7);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().ContainSingle();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(0);
    }

    [TestMethod]
    public async Task SceneSaveConflictPreservesDirtySourceAndHistory()
    {
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.GetSceneSourceVersion(It.IsAny<Scene>())).Returns(value: (SceneSourceVersion?)null);
        _ = manager.Setup(value => value.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .ThrowsAsync(new StorageWriteConflictException("Changed externally"));
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var node = scene.RootNodes[0];
        var context = CreateContext(scene);
        ConfigureTransformSessionPropertySync(fixture, scene, node, []);
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [node.Id], "PositionX");
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(7f), session).ConfigureAwait(false);

        var result = await fixture.Sut.SaveSceneAsync(context).ConfigureAwait(false);

        _ = result.IsConflict.Should().BeTrue();
        _ = result.Succeeded.Should().BeFalse();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.Metadata.SavedVersion.Should().Be(0);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(7);
        _ = fixture.Results.Published.Should().Contain(result => result.Diagnostics.Any(diagnostic => string.Equals(diagnostic.Code, "OXE.DOCUMENT.Conflict", StringComparison.Ordinal)));
    }

    [TestMethod]
    public async Task SceneSaveCopyUsesDistinctIdentityAndKeepsOriginalDirty()
    {
        SceneSaveSnapshot? saved = null;
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.GetSceneSourceVersion(It.IsAny<Scene>())).Returns(value: (SceneSourceVersion?)null);
        _ = manager.Setup(value => value.CreateSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Callback<SceneSaveSnapshot>(snapshot => saved = snapshot).ReturnsAsync(value: true);
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;

        var result = await fixture.Sut.SaveSceneCopyAsync(context, "Copy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = saved.Should().NotBeNull();
        _ = saved!.SceneId.Should().NotBe(scene.Id);
        _ = saved.SceneName.Should().Be("Copy");
        _ = scene.Project.Scenes.Should().Contain(copy => copy.Id == saved.SceneId);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.Metadata.SavedVersion.Should().Be(0);
        _ = scene.Name.Should().Be("Source");
    }

    private static Scene CreateSaveScene()
    {
        var project = new Project(new ProjectInfo("Tests", Category.Games, "H:/SceneSaveTests", "preview.png")) { Name = "Tests" };
        var scene = new Scene(project) { Name = "Source" };
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Node" });
        project.Scenes.Add(scene);
        return scene;
    }
}
