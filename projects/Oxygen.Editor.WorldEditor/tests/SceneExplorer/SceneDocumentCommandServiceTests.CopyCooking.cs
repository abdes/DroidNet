// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Verifies saved-copy scheduling at the scene document owner.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>A scene copy schedules only the acknowledged copy identity and bytes.</summary>
    /// <returns>The asynchronous scene-copy regression.</returns>
    [TestMethod]
    public async Task SceneSaveCopySchedulesOnlyItsAcknowledgedSource()
    {
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;
        SceneSaveSnapshot? written = null;
        var copyPath = Path.Combine(scene.Project.ProjectInfo.Location!, "Content", "Scenes", "Copy.oscene.json");
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.CreateSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Callback<SceneSaveSnapshot>(snapshot => written = snapshot).ReturnsAsync(value: true);
        _ = manager.Setup(value => value.GetSceneSourceVersion(It.IsAny<Scene>()))
            .Returns((Scene target) => written is not null && target.Id == written.SceneId
                ? new SceneSourceVersion(copyPath, new FileVersion(Exists: true, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(written.Json))))) : null);
        var automatic = new Mock<IAutomaticCookService>();
        var fixture = CreateFixture(projectManager: manager.Object, automaticCooking: automatic.Object);

        var result = await fixture.Sut.SaveSceneCopyAsync(context, "Copy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = written.Should().NotBeNull();
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(written!.Json)));
        automatic.Verify(value => value.NotifySaved(copyPath, hash, contentChanged: true), Times.Once);
        automatic.VerifyNoOtherCalls();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = written.SceneId.Should().NotBe(scene.Id);
        _ = scene.Name.Should().Be("Source");
    }

    /// <summary>Unacknowledged copy writes never schedule derived work.</summary>
    /// <param name="throws">Whether persistence throws instead of returning failure.</param>
    /// <returns>The asynchronous failed-copy regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FailedSceneSaveCopyDoesNotScheduleCooking(bool throws)
    {
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(value => value.CreateSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Returns(throws ? Task.FromException<bool>(new IOException("Copy failed")) : Task.FromResult(false));
        var automatic = new Mock<IAutomaticCookService>();
        var fixture = CreateFixture(projectManager: manager.Object, automaticCooking: automatic.Object);
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;

        var result = await fixture.Sut.SaveSceneCopyAsync(context, "Copy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        automatic.VerifyNoOtherCalls();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = scene.Project.Scenes.Should().ContainSingle();
    }
}
