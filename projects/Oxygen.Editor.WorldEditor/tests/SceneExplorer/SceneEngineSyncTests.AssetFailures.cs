// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    public async Task AssetFailure_PublishesCurrentOperationAndDropsStaleOrDeletedTargets()
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        RuntimeWorldRequest? captured = null;
        _ = commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                captured = request;
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);
            });
        var current = true;
        _ = commands.Setup(value => value.IsCurrentAssetRequest(It.IsAny<RuntimeWorldRequest>())).Returns(() => current);
        var results = new List<OperationResult>();
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(results.Add);
        using var sut = new SceneEngineSync(engine.Object, operationResults: publisher.Object);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        _ = await sut.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await sut.UpdateMaterialSlotAsync(scene, node, 0, new Uri("asset:///Content/Materials/Missing.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = new RuntimeAssetLoadFailedEventArgs(captured!, 71, "Asset path could not be resolved");

        commands.Raise(value => value.AssetLoadFailed += null, failure);
        current = false;
        commands.Raise(value => value.AssetLoadFailed += null, failure);
        current = true;
        _ = scene.RootNodes.Remove(node);
        commands.Raise(value => value.AssetLoadFailed += null, failure);

        var result = results.Should().ContainSingle().Subject;
        _ = result.OperationId.Should().Be(failure.Request.OperationId);
        _ = result.AffectedScope.NodeId.Should().Be(node.Id);
        _ = result.AffectedScope.SceneId.Should().Be(scene.Id);
        _ = result.Diagnostics.Should().ContainSingle().Which.Domain.Should().Be(FailureDomain.LiveSync);
        _ = result.Message.Should().Be(failure.Message);
    }
}
