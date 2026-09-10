// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    public async Task DeleteDuringFullProjection_RemovesTheFrozenNodeBeforeReadiness()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(1);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(node, metadata, 7);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Remove(node);
        metadata.IsDirty = true;
        await fixture.Sync.RemoveNodeAsync(scene, node.Id).ConfigureAwait(false);

        creationCompletion.SetResult(Accepted(request));
        _ = (await syncing.ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().BeEmpty();
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeRemoveSceneNode>().Should().ContainSingle().Which.NodeId.Should().Be(node.Id);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task NodeAddedDuringProjection_IsCreatedBeforeItsLaterPropertyEdit()
    {
        using var fixture = new ReplayFixture();
        var (scene, _, metadata) = fixture.RegisterScene(0);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var first = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var added = new SceneNode(scene) { Name = "Added during sync" };
        scene.RootNodes.Add(added);
        metadata.IsDirty = true;
        var adding = fixture.Sync.CreateNodeAsync(added);
        SetPosition(added, metadata, 9);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, added, [CreateTransformEntry(9)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = adding.IsCompleted.Should().BeFalse();

        creationCompletion.SetResult(Accepted(first));
        _ = (await syncing.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        await adding.ConfigureAwait(false);

        fixture.Commands.Verify(value => value.CreateNodeAsync(It.Is<RuntimeWorldRequest>(request => request.Command is RuntimeCreateNode && ((RuntimeCreateNode)request.Command).NodeId == added.Id), It.IsAny<CancellationToken>()), Times.Once);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().ContainSingle().Which.Entries.Should().ContainSingle().Which.Value.Should().Be(9);
        _ = added.IsActive.Should().BeTrue();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task ComponentRemovedDuringProjection_DiscardsItsQueuedFieldsAndDetachesTheFrozenComponent()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(0);
        var camera = new PerspectiveCamera { Name = "Camera", FieldOfView = 60 };
        node.Components.Add(camera);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        camera.FieldOfView = 70;
        metadata.IsDirty = true;
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [new(EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.FieldOfViewYRadians, 70 * MathF.PI / 180)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Components.Remove(camera);
        metadata.IsDirty = true;
        _ = await fixture.Sync.DetachCameraAsync(scene, node.Id, this.TestContext.CancellationToken).ConfigureAwait(false);

        creationCompletion.SetResult(Accepted(request));
        _ = (await syncing.ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().BeEmpty();
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeDetachCamera>().Should().ContainSingle();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task InactiveTopologyRequest_CannotUseAnotherScenesNodeIdentity()
    {
        using var fixture = new ReplayFixture();
        var (inactive, _, _) = fixture.RegisterScene(1);
        var (active, node, _) = fixture.RegisterScene(2);
        _ = await fixture.Sync.SyncSceneAsync(active, this.TestContext.CancellationToken).ConfigureAwait(false);

        await fixture.Sync.RemoveNodeAsync(inactive, node.Id).ConfigureAwait(false);
        await fixture.Sync.ReparentNodeAsync(inactive, node.Id, newParentGuid: null).ConfigureAwait(false);

        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeRemoveSceneNode>().Should().BeEmpty();
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeReparentSceneNode>().Should().ContainSingle("only the initial full projection reparents this node");
    }

    [TestMethod]
    public async Task RuntimeReturning_AutomaticallyProjectsTheRequestedCurrentDocument()
    {
        using var fixture = new ReplayFixture();
        fixture.State = EngineServiceState.NoEngine;
        var (scene, node, metadata) = fixture.RegisterScene(1);
        _ = (await fixture.Sync.SyncSceneWhenReadyAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        SetPosition(node, metadata, 9);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        var completed = new TaskCompletionSource<SceneSynchronizationCompletedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Sync.SceneSynchronized += (_, args) => completed.SetResult(args);

        fixture.State = EngineServiceState.Running;
        var runId = fixture.Commands.Object.RunId;
        fixture.Engine.Raise(value => value.StateChanged += null, new EngineStateChangedEventArgs(runId, EngineServiceState.Starting, EngineServiceState.Running));
        var synchronized = await completed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = synchronized.Scene.Should().BeSameAs(scene);
        _ = synchronized.Metadata.Should().BeSameAs(metadata);
        _ = synchronized.Target.RunId.Should().Be(runId);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetLocalTransform>().Should().ContainSingle().Which.Position.X.Should().Be(9);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        fixture.Engine.Verify(value => value.StartAsync(), Times.Never);
    }

    [TestMethod]
    public async Task DisposingDuringProjection_RetiresTheDocumentAndFinishesWithoutDisposingAnOwnedWait()
    {
        using var fixture = new ReplayFixture();
        var (scene, _, metadata) = fixture.RegisterScene(1);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Commands.Setup(value => value.InvalidateScene(request.Target))
            .Callback(() => creationCompletion.SetResult(new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Rejected)));

        fixture.Sync.Dispose();
        var result = await sync.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        _ = fixture.Sync.RegisterDocument(scene, metadata).Should().BeFalse();
        fixture.Engine.Verify(value => value.DisposeAsync(), Times.Never);
    }

    [TestMethod]
    public async Task EnvironmentDuringFullSnapshot_ReplaysAfterEarlierProperties()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(0);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(node, metadata, 7);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.2f, 0.4f, 0.6f);
        var environment = scene.Environment with { BackgroundColor = color };
        scene.SetEnvironment(environment);
        metadata.IsDirty = true;

        var pending = await fixture.Sync.UpdateEnvironmentAsync(scene, environment, fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = pending.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(2);
        creationCompletion.SetResult(Accepted(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeTrue();

        var commands = fixture.Requests.Select(value => value.Command).ToList();
        _ = commands.OfType<RuntimeSetBackgroundColor>().Select(value => value.Color).Should().Equal(Vector3.Zero, color);
        _ = commands.FindIndex(value => value is RuntimeSetProperties).Should().BeLessThan(commands.FindLastIndex(value => value is RuntimeSetBackgroundColor));
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task EnvironmentReplayFailure_RetainsBackgroundScopeAndRecoversWithCurrentSnapshot()
    {
        using var fixture = new ReplayFixture();
        var (scene, _, metadata) = fixture.RegisterScene(0);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.2f, 0.4f, 0.6f);
        var environment = scene.Environment with { BackgroundColor = color };
        scene.SetEnvironment(environment);
        metadata.IsDirty = true;
        _ = await fixture.Sync.UpdateEnvironmentAsync(scene, environment, fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.RejectedBackground = color;

        creationCompletion.SetResult(Accepted(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        var diagnostic = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle().Which;
        _ = diagnostic.AffectedEntity!.ComponentName.Should().Be(nameof(SceneEnvironmentData.BackgroundColor));
        _ = diagnostic.Code.Should().Be(LiveSyncDiagnosticCodes.EnvironmentBackgroundRejected);
        fixture.RejectedBackground = null;
        fixture.ResumeNodeCreation();
        _ = (await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetBackgroundColor>().Last().Color.Should().Be(color);
    }

    [TestMethod]
    public async Task FullSnapshot_KeepsItsCapturedValueAndReplaysNewerAuthoring()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(1);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(node, metadata, 9);
        var stamp = fixture.Sync.CaptureRevision(scene, metadata);

        var pending = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], stamp, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = pending.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        creationCompletion.SetResult(Accepted(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeTrue();

        var initial = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetLocalTransform>().Should().ContainSingle().Which;
        _ = initial.Position.X.Should().Be(1);
        var replay = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().ContainSingle().Which;
        _ = replay.Entries.Should().ContainSingle().Which.Value.Should().Be(9);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        _ = node.IsActive.Should().BeTrue();
    }

    [TestMethod]
    public async Task FailedReplay_RemainsVisibleUntilACompleteCurrentSnapshotSucceeds()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(1);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(node, metadata, 9);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.RejectProperties = true;

        creationCompletion.SetResult(Accepted(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        var failure = fixture.Results.Should().ContainSingle().Which;
        _ = failure.Message.Should().Be("Controlled property rejection");
        _ = failure.AffectedScope.DocumentId.Should().Be(metadata.DocumentId);
        fixture.RejectProperties = false;
        fixture.ResumeNodeCreation();
        _ = (await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetLocalTransform>().Last().Position.X.Should().Be(9);
        _ = fixture.Results.Should().ContainSingle("recovery does not erase operation history");
    }

    [TestMethod]
    public async Task OlderDelayedPayload_CannotOverwriteANewerAcceptedRevision()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, metadata) = fixture.RegisterScene(0);
        _ = await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(node, metadata, 1);
        var older = fixture.Sync.CaptureRevision(scene, metadata);
        SetPosition(node, metadata, 2);
        var newer = fixture.Sync.CaptureRevision(scene, metadata);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(2)], newer, this.TestContext.CancellationToken).ConfigureAwait(false);

        var stale = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(1)], older, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = stale.Status.Should().Be(SyncStatus.Cancelled);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().ContainSingle().Which.Entries
            .Should().ContainSingle().Which.Value.Should().Be(2);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task InactiveScene_BuffersItsOwnValuesAndDoesNotDispatchIntoAnotherScene()
    {
        using var fixture = new ReplayFixture();
        var (first, firstNode, firstMetadata) = fixture.RegisterScene(1);
        var (second, _, _) = fixture.RegisterScene(2);
        _ = await fixture.Sync.SyncSceneAsync(second, this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(firstNode, firstMetadata, 7);

        var pending = await fixture.Sync.UpdatePropertiesAsync(first, firstNode, [CreateTransformEntry(7)], fixture.Sync.CaptureRevision(first, firstMetadata), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = pending.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Requests.Should().OnlyContain(request => request.Target.SceneId == second.Id);
        _ = fixture.Sync.GetPendingPropertySyncCount(first.Id).Should().Be(1);
        _ = (await fixture.Sync.SyncSceneAsync(first, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Sync.GetPendingPropertySyncCount(first.Id).Should().Be(0);
        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetLocalTransform>().Last().Position.X.Should().Be(7);
    }

    [TestMethod]
    public async Task DeletedQueuedNode_IsNotRecreatedByReplay()
    {
        using var fixture = new ReplayFixture();
        fixture.State = EngineServiceState.Ready;
        var (scene, node, metadata) = fixture.RegisterScene(1);
        SetPosition(node, metadata, 7);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Sync.CaptureRevision(scene, metadata), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Remove(node);
        metadata.IsDirty = true;
        fixture.State = EngineServiceState.Running;

        _ = (await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Requests.Select(value => value.Command).OfType<RuntimeSetProperties>().Should().BeEmpty();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        fixture.Commands.Verify(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    [TestMethod]
    public async Task ClosedDocument_CannotRegisterAfterLoadOrInvalidateReopenedLifetime()
    {
        using var fixture = new ReplayFixture();
        fixture.State = EngineServiceState.Ready;
        var (scene, node, oldMetadata) = fixture.RegisterScene(1);
        var old = fixture.Sync.CaptureRevision(scene, oldMetadata);
        fixture.Sync.CloseDocument(oldMetadata);
        _ = fixture.Sync.RegisterDocument(scene, oldMetadata).Should().BeFalse();
        var reopened = new SceneDocumentMetadata(scene.Id);
        _ = fixture.Sync.RegisterDocument(scene, reopened).Should().BeTrue();
        SetPosition(node, reopened, 9);
        var current = fixture.Sync.CaptureRevision(scene, reopened);
        _ = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], current, this.TestContext.CancellationToken).ConfigureAwait(false);

        fixture.Sync.CloseDocument(oldMetadata);
        var stale = await fixture.Sync.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(1)], old, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = stale.Status.Should().Be(SyncStatus.Cancelled);
        _ = fixture.Sync.CaptureRevision(scene, oldMetadata).DocumentLifetime.Should().BeEmpty();
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        fixture.State = EngineServiceState.Running;
        _ = await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Requests.Should().OnlyContain(request => request.Target.DocumentLifetime == current.DocumentLifetime);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
    }

    private static RuntimeCommandResult Accepted(RuntimeWorldRequest request)
        => new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);

    private static void SetPosition(SceneNode node, SceneDocumentMetadata metadata, float value)
    {
        node.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(value, 0, 0);
        metadata.IsDirty = true;
    }

    private sealed partial class ReplayFixture : IDisposable
    {
        public ReplayFixture()
        {
            var engine = this.Engine;
            _ = engine.SetupGet(value => value.State).Returns(() => this.State);
            _ = engine.SetupGet(value => value.WorldCommands).Returns(this.Commands.Object);
            _ = this.Commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
                .Returns((RuntimeWorldRequest request, CancellationToken _) =>
                {
                    this.Requests.Enqueue(request);
                    return request.Command is RuntimeSetBackgroundColor background && this.RejectedBackground == background.Color
                        ? new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Rejected, "Controlled background rejection")
                        : this.RejectProperties && request.Command is RuntimeSetProperties
                        ? new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Rejected, "Controlled property rejection")
                        : Accepted(request);
                });
            var publisher = new Mock<IOperationResultPublisher>();
            _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
            this.Sync = new SceneEngineSync(engine.Object, operationResults: publisher.Object);
        }

        public Mock<IRuntimeWorldCommands> Commands { get; } = CreateManagedWorld();

        public Mock<IEngineService> Engine { get; } = new();

        public SceneEngineSync Sync { get; }

        public EngineServiceState State { get; set; } = EngineServiceState.Running;

        public bool RejectProperties { get; set; }

        public Vector3? RejectedBackground { get; set; }

        public ConcurrentQueue<RuntimeWorldRequest> Requests { get; } = new();

        public ConcurrentQueue<OperationResult> Results { get; } = new();

        public void Dispose() => this.Sync.Dispose();

        public (Scene scene, SceneNode node, SceneDocumentMetadata metadata) RegisterScene(float x)
        {
            var scene = CreateScene();
            var node = new SceneNode(scene) { Name = "Cube" };
            scene.RootNodes.Add(node);
            node.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(x, 0, 0);
            var metadata = new SceneDocumentMetadata(scene.Id);
            _ = this.Sync.RegisterDocument(scene, metadata);
            return (scene, node, metadata);
        }

        public (TaskCompletionSource<RuntimeWorldRequest> request, TaskCompletionSource<RuntimeCommandResult> completion) DelayNodeCreation()
        {
            var request = new TaskCompletionSource<RuntimeWorldRequest>(TaskCreationOptions.RunContinuationsAsynchronously);
            var completion = new TaskCompletionSource<RuntimeCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
            _ = this.Commands.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
                .Returns((RuntimeWorldRequest value, CancellationToken cancellationToken) =>
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    return request.TrySetResult(value) ? completion.Task : Task.FromResult(Accepted(value));
                });
            return (request, completion);
        }

        public void ResumeNodeCreation()
            => _ = this.Commands.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((RuntimeWorldRequest request, CancellationToken _) => Accepted(request));
    }
}
