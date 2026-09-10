// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task PendingMetadataNotification_DoesNotDelayPropertyPublicationPastFullSync()
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        var requests = new List<RuntimeWorldRequest>();
        var world = CreateReplayWorld(requests);
        var creation = new TaskCompletionSource<RuntimeCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        RuntimeWorldRequest? creationRequest = null;
        _ = world.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Callback<RuntimeWorldRequest, CancellationToken>((request, _) => creationRequest = request).Returns(creation.Task);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(world.Object);
        using var synchronization = new SceneEngineSync(engine.Object);
        var fixture = CreateFixture(synchronization);
        var notification = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.DocumentService.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).Returns(notification.Task);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = synchronization.RegisterDocument(scene, context.Metadata);
        var rebuilding = synchronization.SyncSceneAsync(scene, this.TestContext.CancellationToken);

        var editing = fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(9), EditSessionToken.OneShot);
        _ = editing.IsCompleted.Should().BeFalse();
        _ = synchronization.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        creation.SetResult(new(creationRequest!.OperationId, creationRequest.Target.RunId, RuntimeCommandStatus.Accepted));
        _ = (await rebuilding.ConfigureAwait(false)).Should().BeTrue();

        _ = requests.Select(request => request.Command).OfType<RuntimeSetProperties>().Should().ContainSingle().Which.Entries
            .Should().ContainSingle().Which.Value.Should().Be(9);
        notification.SetResult(true);
        _ = await editing.ConfigureAwait(false);
    }

    [TestMethod]
    public async Task OfflineEditAndUndo_ReconnectProjectsCurrentAuthoringAndRedoRemainsLive()
    {
        var state = EngineServiceState.Ready;
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(() => state);
        var requests = new List<RuntimeWorldRequest>();
        var world = CreateReplayWorld(requests);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(world.Object);
        using var synchronization = new SceneEngineSync(engine.Object);
        var fixture = CreateFixture(synchronization);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = synchronization.RegisterDocument(scene, context.Metadata);

        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(7), EditSessionToken.OneShot).ConfigureAwait(false);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = synchronization.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        state = EngineServiceState.Running;
        _ = (await synchronization.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = requests.Select(request => request.Command).OfType<RuntimeSetLocalTransform>().Should().ContainSingle().Which.Position.Should().Be(Vector3.Zero);
        _ = requests.Select(request => request.Command).OfType<RuntimeSetProperties>().Should().BeEmpty("the snapshot supersedes both the offline edit and its undo");
        _ = synchronization.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = requests.Select(request => request.Command).OfType<RuntimeSetProperties>().Should().ContainSingle().Which.Entries
            .Should().ContainSingle().Which.Value.Should().Be(7);
        _ = context.Metadata.ChangeVersion.Should().Be(3);
    }

    private static Mock<IRuntimeWorldCommands> CreateReplayWorld(List<RuntimeWorldRequest> requests)
    {
        var world = new Mock<IRuntimeWorldCommands>();
        _ = world.SetupGet(value => value.RunId).Returns(Guid.NewGuid());
        _ = world.Setup(value => value.ActivateSceneAsync(It.IsAny<Guid>(), It.IsAny<RuntimeSceneTarget>(), It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((Guid operationId, RuntimeSceneTarget target, string _, CancellationToken _) => new RuntimeCommandResult(operationId, target.RunId, RuntimeCommandStatus.Accepted));
        _ = world.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((RuntimeWorldRequest request, CancellationToken _) => new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted));
        _ = world.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                requests.Add(request);
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);
            });
        return world;
    }
}
