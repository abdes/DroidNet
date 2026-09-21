// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneEngineSyncTests
{
    /// <summary>Mask requests retain source ownership and report failures against the scene.</summary>
    /// <param name="mount">The authored source mount.</param>
    /// <returns>The synchronization check.</returns>
    [TestMethod]
    [DataRow("Content")]
    [DataRow("Lighting")]
    public async Task ExposureMaskUsesItsMountAndPublishesSceneScopedFailure(string mount)
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        var requests = new List<RuntimeWorldRequest>();
        _ = commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                requests.Add(request);
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);
            });
        var current = true;
        _ = commands.Setup(value => value.IsCurrentAssetFailure(It.IsAny<RuntimeAssetLoadFailedEventArgs>())).Returns(() => current);
        var results = new List<OperationResult>();
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(results.Add);
        using var sut = new SceneEngineSync(engine.Object, operationResults: publisher.Object);
        var projectRoot = Path.Combine(Path.GetTempPath(), "Mask project");
        var project = new Project(new ProjectInfo("Mask project", Category.Games, projectRoot, "preview.png")) { Name = "Mask project" };
        var scene = new Scene(project) { Name = "Masked scene" };
        var mask = new Uri($"asset:///{mount}/Textures/Meter.otex.json");
        scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { AutoExposureMeteringMask = mask } });
        _ = sut.RegisterDocument(scene, new SceneDocumentMetadata(scene.Id));

        _ = (await sut.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var request = requests.Should().ContainSingle(value => value.Command is RuntimeSetEnvironment).Subject;
        var environment = (RuntimeSetEnvironment)request.Command;
        _ = environment.AutoExposureMeteringMask.Should().Be(new RuntimeTextureReference(mask, Path.Combine(projectRoot, ".cooked", mount), "Textures/Meter.otex"));
        var failure = new RuntimeAssetLoadFailedEventArgs(request, 71, "Texture could not be loaded");
        commands.Raise(value => value.AssetLoadFailed += null, failure);
        current = false;
        commands.Raise(value => value.AssetLoadFailed += null, failure);

        var result = results.Should().ContainSingle().Subject;
        _ = result.OperationId.Should().Be(request.OperationId);
        _ = result.AffectedScope.SceneId.Should().Be(scene.Id);
        _ = result.AffectedScope.NodeId.Should().BeNull();
        _ = result.AffectedScope.ComponentType.Should().Be(nameof(PostProcessEnvironmentData));
        _ = result.Diagnostics.Should().ContainSingle().Which.AffectedPath.Should().Be(mask.AbsoluteUri);
    }
}
