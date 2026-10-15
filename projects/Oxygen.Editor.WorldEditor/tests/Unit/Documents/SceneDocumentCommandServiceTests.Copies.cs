// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies saving a copy of a scene at the scene document owner.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>A scene copy writes a new scene and leaves the open scene and its unsaved edits as they were.</summary>
    /// <returns>The asynchronous scene-copy regression.</returns>
    [TestMethod]
    public async Task SceneSaveCopyWritesOnlyTheCopy()
    {
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;
        SceneSaveSnapshot? written = null;
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.CreateSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Callback<SceneSaveSnapshot>(snapshot => written = snapshot).ReturnsAsync(value: true);
        var fixture = CreateFixture(projectManager: manager.Object);

        var result = await fixture.Sut.SaveSceneCopyAsync(context, "Copy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = written.Should().NotBeNull();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = written!.SceneId.Should().NotBe(scene.Id);
        _ = scene.Name.Should().Be("Source");
    }

    /// <summary>A copy that is not written adds no scene and leaves the open scene unsaved.</summary>
    /// <param name="throws">Whether persistence throws instead of returning failure.</param>
    /// <returns>The asynchronous failed-copy regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FailedSceneSaveCopyAddsNoScene(bool throws)
    {
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.CreateSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Returns(throws ? Task.FromException<bool>(new IOException("Copy failed")) : Task.FromResult(false));
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;

        var result = await fixture.Sut.SaveSceneCopyAsync(context, "Copy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = scene.Project.Scenes.Should().ContainSingle();
    }
}
