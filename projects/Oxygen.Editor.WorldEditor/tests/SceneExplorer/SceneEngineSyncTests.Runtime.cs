// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    [DataRow(RuntimeCommandStatus.Accepted, SyncStatus.Accepted)]
    [DataRow(RuntimeCommandStatus.Rejected, SyncStatus.Rejected)]
    [DataRow(RuntimeCommandStatus.Unavailable, SyncStatus.SkippedNotRunning)]
    [DataRow(RuntimeCommandStatus.Cancelled, SyncStatus.Cancelled)]
    [DataRow(RuntimeCommandStatus.Failed, SyncStatus.Failed)]
    public async Task ManagedWorldSubstitute_DrivesPropertyOutcomes(RuntimeCommandStatus runtimeStatus, SyncStatus expected)
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        using var sut = new SceneEngineSync(engine.Object);
        var scene = CreateScene();
        _ = sut.RegisterDocument(scene, new SceneDocumentMetadata(scene.Id));
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        _ = (await sut.SyncSceneAsync(scene, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        RuntimeWorldRequest? captured = null;
        _ = commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                captured = request;
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, runtimeStatus, "Controlled outcome");
            });

        var outcome = await sut.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(42.5f)], cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(expected);
        _ = captured.Should().NotBeNull();
        _ = captured!.Target.SceneId.Should().Be(scene.Id);
        _ = captured.Target.DocumentLifetime.Should().NotBeEmpty();
        _ = captured.Target.ActivationId.Should().NotBeEmpty();
        var command = captured.Command.Should().BeOfType<RuntimeSetProperties>().Subject;
        _ = command.NodeId.Should().Be(node.Id);
        _ = command.Entries.Should().ContainSingle().Which.Value.Should().Be(42.5f);
        _ = outcome.Scope.NodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public async Task SceneProjection_PreservesCameraUnitsAndMaterialClearWithoutNativeFacades()
    {
        var commands = CreateManagedWorld();
        var requests = new List<RuntimeWorldRequest>();
        _ = commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                requests.Add(request);
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);
            });
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        using var sut = new SceneEngineSync(engine.Object);
        var scene = CreateScene();
        _ = sut.RegisterDocument(scene, new SceneDocumentMetadata(scene.Id));
        var node = new SceneNode(scene) { Name = "Camera" };
        scene.RootNodes.Add(node);
        node.Components.Add(new PerspectiveCamera { Name = "Perspective Camera", FieldOfView = 60, AspectRatio = 1.5f, NearPlane = 0.25f, FarPlane = 500 });

        _ = (await sut.SyncSceneAsync(scene, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var camera = requests.Select(value => value.Command).OfType<RuntimeAttachPerspectiveCamera>().Should().ContainSingle().Subject;
        _ = camera.NodeId.Should().Be(node.Id);
        _ = camera.FieldOfViewYRadians.Should().BeApproximately(MathF.PI / 3, 0.00001f);
        _ = camera.AspectRatio.Should().Be(1.5f);
        _ = camera.NearPlane.Should().Be(0.25f);
        _ = camera.FarPlane.Should().Be(500);
        var cleared = await sut.UpdateMaterialSlotAsync(scene, node, 0, materialUri: null, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cleared.Status.Should().Be(SyncStatus.Accepted);
        _ = requests.Select(value => value.Command).OfType<RuntimeSetMaterialOverride>().Should().ContainSingle().Which.MaterialPath.Should().BeNull();
    }

    private static Mock<IRuntimeWorldCommands> CreateManagedWorld()
    {
        var world = new Mock<IRuntimeWorldCommands>(MockBehavior.Strict);
        _ = world.SetupGet(value => value.RunId).Returns(Guid.NewGuid());
        _ = world.Setup(value => value.ActivateSceneAsync(It.IsAny<Guid>(), It.IsAny<RuntimeSceneTarget>(), It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((Guid operation, RuntimeSceneTarget target, string _, CancellationToken _) => new RuntimeCommandResult(operation, target.RunId, RuntimeCommandStatus.Accepted));
        _ = world.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((RuntimeWorldRequest request, CancellationToken _) => new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted));
        _ = world.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) => new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted));
        _ = world.Setup(value => value.InvalidateScene(It.IsAny<RuntimeSceneTarget>()));
        return world;
    }
}
