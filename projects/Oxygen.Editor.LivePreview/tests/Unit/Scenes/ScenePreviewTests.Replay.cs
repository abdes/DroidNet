// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task RepeatedRunningNotification_KeepsTheProjectionWithoutRebuildingIt()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (_, _, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var ready = new List<ScenePreviewEventArgs>();
        var first = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Preview.ReadyChanged += (_, args) =>
        {
            ready.Add(args);
            _ = first.TrySetResult();
        };
        fixture.Engine.ChangeState(EngineServiceState.Running);
        await first.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var requests = fixture.Requests.ToArray();

        fixture.Engine.RaiseStateChanged();
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = ready.Should().ContainSingle().Which.Document.Should().Be(document.Id);
        _ = fixture.Preview.IsReady(document.Id).Should().BeTrue();
        _ = fixture.Requests.Should().Equal(requests);
    }

    [TestMethod]
    public async Task DeleteDuringFullProjection_RemovesTheFrozenNodeBeforeReadiness()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 7);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Remove(node);
        _ = fixture.Preview.AdvanceRevision(scene);
        await fixture.Preview.RemoveNodeHierarchiesAsync(scene, [node.Id]).ConfigureAwait(false);

        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await syncing.ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.Transform).Should().BeEmpty("the queued edit belonged to a node deleted before readiness");
        _ = fixture.World.Commands.OfType<RuntimeRemoveSceneNodes>().Should().ContainSingle().Which.Nodes.Should().Equal(node.Id);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task NodeAddedDuringProjection_IsCreatedBeforeItsLaterPropertyEdit()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var first = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var added = new SceneNode(scene, "Added during sync");
        scene.RootNodes.Add(added);
        _ = fixture.Preview.AdvanceRevision(scene);
        var adding = fixture.Preview.CreateNodeAsync(added);
        SetPosition(fixture.Preview, added, 9);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, added, [CreateTransformEntry(9)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = adding.IsCompleted.Should().BeFalse();

        creationCompletion.SetResult(FakeWorld.Accept(first));
        _ = (await syncing.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        await adding.ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeCreateNode>().Count(command => command.NodeId == added.Id).Should().Be(1);
        _ = fixture.World.PropertiesOf(added.Id, RuntimeComponentId.Transform).Should().Equal(PositionX(9));
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task ComponentRemovedDuringProjection_DiscardsItsQueuedFieldsAndDetachesTheFrozenComponent()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var camera = new PerspectiveCamera { Name = "Camera", FieldOfView = 60 };
        node.Components.Add(camera);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var syncing = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        camera.FieldOfView = 70;
        _ = fixture.Preview.AdvanceRevision(scene);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [new(RuntimeComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.FieldOfViewYRadians, 70 * MathF.PI / 180)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Components.Remove(camera);
        _ = fixture.Preview.AdvanceRevision(scene);
        _ = await fixture.Preview.DetachCameraAsync(scene, node.Id, this.TestContext.CancellationToken).ConfigureAwait(false);

        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await syncing.ConfigureAwait(false)).Should().BeTrue();

        // The initial projection keeps the frozen physical-camera recipe; the queued field of view is discarded.
        _ = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.PerspectiveCamera).Should().Equal(
            new RuntimePropertyValue(RuntimeComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.ApertureF, camera.ApertureF),
            new RuntimePropertyValue(RuntimeComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.ShutterRate, camera.ShutterRate),
            new RuntimePropertyValue(RuntimeComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.Iso, camera.Iso));
        _ = fixture.World.Commands.OfType<RuntimeDetachCamera>().Should().ContainSingle();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task InactiveTopologyRequest_CannotUseAnotherScenesNodeIdentity()
    {
        using var fixture = new PreviewFixture();
        var (inactive, _, _) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (active, node, _) = await fixture.OpenCubeAsync(2).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(active, this.TestContext.CancellationToken).ConfigureAwait(false);

        await fixture.Preview.RemoveNodeHierarchiesAsync(inactive, [node.Id]).ConfigureAwait(false);
        await fixture.Preview.ReparentHierarchiesAsync(inactive, [node.Id], newParentGuid: null).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeRemoveSceneNodes>().Should().BeEmpty();
        _ = fixture.World.Commands.OfType<RuntimeReparentSceneNodes>().Should().BeEmpty();
        _ = fixture.World.Commands.OfType<RuntimeReparentSceneNode>().Should().ContainSingle("only the initial full projection reparents this node");
    }

    [TestMethod]
    public async Task EngineThatStartsRunning_ShowsTheOpenDocumentWithItsWaitingFields()
    {
        using var fixture = new PreviewFixture(EngineServiceState.NoEngine);
        var (scene, node, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 9);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        var ready = new TaskCompletionSource<ScenePreviewEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Preview.ReadyChanged += (_, args) => ready.TrySetResult(args);

        fixture.Engine.ChangeState(EngineServiceState.Running);
        var shown = await ready.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = shown.Document.Should().Be(document.Id);
        _ = fixture.Preview.IsReady(document.Id).Should().BeTrue();
        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>().Should().ContainSingle().Which.Position.X.Should().Be(9);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = fixture.Engine.Starts.Should().Be(0, "the preview never starts the engine");
    }

    [TestMethod]
    public async Task DisposingDuringProjection_RetiresTheDocumentAndFinishes()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.World.OnInvalidateScene = target =>
        {
            if (target == request.Target)
            {
                _ = creationCompletion.TrySetResult(FakeWorld.Result(request, RuntimeCommandStatus.Rejected));
            }
        };

        fixture.Preview.Dispose();
        var result = await sync.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        _ = fixture.Preview.RegisterDocument(SceneContent.Of(document), OwnerThread.Capture()).Should().BeFalse();
        _ = fixture.Preview.IsReady(document.Id).Should().BeFalse();
    }

    [TestMethod]
    public async Task EnvironmentDuringFullSnapshot_ReplaysAfterEarlierProperties()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 7);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.2f, 0.4f, 0.6f);
        var environment = scene.Environment with { Background = new() { Enabled = true, ColorRgb = color } };
        scene.SetEnvironment(environment);
        _ = fixture.Preview.AdvanceRevision(scene);

        var pending = await fixture.Preview.UpdateEnvironmentAsync(scene, environment, fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = pending.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(2);
        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeTrue();

        var commands = fixture.World.Commands.ToList();
        _ = commands.OfType<RuntimeSetEnvironment>().Select(value => value.Background.ColorRgb).Should().Equal(Vector3.Zero, color);
        _ = commands.FindIndex(value => value is RuntimeSetProperties).Should().BeLessThan(commands.FindLastIndex(value => value is RuntimeSetEnvironment));
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task EnvironmentReplayFailure_RetainsBackgroundScopeAndRecoversWithCurrentSnapshot()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.2f, 0.4f, 0.6f);
        var environment = scene.Environment with { Background = new() { Enabled = true, ColorRgb = color } };
        scene.SetEnvironment(environment);
        _ = fixture.Preview.AdvanceRevision(scene);
        _ = await fixture.Preview.UpdateEnvironmentAsync(scene, environment, fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.RejectedBackground = color;

        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(1);
        var diagnostic = fixture.Results.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle().Which;
        _ = diagnostic.AffectedEntity!.ComponentType.Should().Be(nameof(SceneEnvironmentData));
        _ = diagnostic.Code.Should().Be(LiveSyncDiagnosticCodes.EnvironmentRejected);
        fixture.RejectedBackground = null;
        fixture.ResumeNodeCreation();
        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = fixture.World.Commands.OfType<RuntimeSetEnvironment>().Last().Background.ColorRgb.Should().Be(color);
    }

    [TestMethod]
    public async Task FullSnapshot_KeepsItsCapturedValueAndReplaysNewerAuthoring()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 9);
        var stamp = fixture.Preview.CaptureRevision(scene);

        var pending = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], stamp, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = pending.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(1);
        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeTrue();

        var initial = fixture.World.Commands.OfType<RuntimeSetLocalTransform>().Should().ContainSingle().Which;
        _ = initial.Position.X.Should().Be(1);
        _ = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.Transform).Should().Equal(PositionX(9));
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = fixture.World.Commands.OfType<RuntimeCreateNode>().Count(command => command.NodeId == node.Id).Should().Be(1);
    }

    [TestMethod]
    public async Task FailedReplay_RemainsVisibleUntilACompleteCurrentSnapshotSucceeds()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (createdRequest, creationCompletion) = fixture.DelayNodeCreation();
        var sync = fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken);
        var request = await createdRequest.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 9);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.RejectedPropertyComponent = RuntimeComponentId.Transform;

        creationCompletion.SetResult(FakeWorld.Accept(request));
        _ = (await sync.ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(1);
        var failure = fixture.Results.Should().ContainSingle().Which;
        _ = failure.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(LiveSyncDiagnosticCodes.TransformRejected);
        _ = failure.AffectedScope.SceneId.Should().Be(scene.Id);
        fixture.RejectedPropertyComponent = null;
        fixture.ResumeNodeCreation();
        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>().Last().Position.X.Should().Be(9);
        _ = fixture.Results.Should().ContainSingle("recovery does not erase operation history");
    }

    [TestMethod]
    public async Task OlderDelayedPayload_CannotOverwriteANewerAcceptedRevision()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 1);
        var older = fixture.Preview.CaptureRevision(scene);
        SetPosition(fixture.Preview, node, 2);
        var newer = fixture.Preview.CaptureRevision(scene);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(2)], newer, this.TestContext.CancellationToken).ConfigureAwait(false);

        var stale = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(1)], older, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = stale.Status.Should().Be(SyncStatus.Cancelled);
        _ = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.Transform).Should().Equal(PositionX(2));
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task InactiveScene_BuffersItsOwnValuesAndDoesNotDispatchIntoAnotherScene()
    {
        using var fixture = new PreviewFixture();
        var (first, firstNode, firstDocument) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var (second, _, _) = await fixture.OpenCubeAsync(2).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(second, this.TestContext.CancellationToken).ConfigureAwait(false);
        SetPosition(fixture.Preview, firstNode, 7);

        var pending = await fixture.Preview.UpdatePropertiesAsync(first, firstNode, [CreateTransformEntry(7)], fixture.Preview.CaptureRevision(first), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = pending.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = fixture.Requests.Should().OnlyContain(request => request.Target.SceneId == second.Id);
        _ = fixture.Preview.PendingFieldCount(firstDocument.Id).Should().Be(1);
        _ = (await fixture.Preview.SyncSceneAsync(first, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Preview.PendingFieldCount(firstDocument.Id).Should().Be(0);
        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>().Last().Position.X.Should().Be(7);
    }

    [TestMethod]
    public async Task DeletedQueuedNode_IsNotRecreatedByReplay()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (scene, node, document) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        SetPosition(fixture.Preview, node, 7);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(7)], fixture.Preview.CaptureRevision(scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Remove(node);
        _ = fixture.Preview.AdvanceRevision(scene);
        fixture.State = EngineServiceState.Running;

        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.Transform).Should().BeEmpty();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = fixture.World.Commands.OfType<RuntimeCreateNode>().Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReopenedDocument_IgnoresTheClosedDocumentsRevisions()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (closedScene, _, closed) = await fixture.OpenCubeAsync(1).ConfigureAwait(false);
        var old = fixture.Preview.CaptureRevision(closedScene);
        var file = closed.File;
        fixture.Documents.Open.Close(closed);
        _ = fixture.Preview.PendingFieldCount(closed.Id).Should().Be(0);

        // Opening the scene's file again gives a new document with a scene of its own.
        var reopened = (await fixture.Documents.Open.OpenAsync(file, this.TestContext.CancellationToken).ConfigureAwait(false)).Document!;
        var scene = PreviewFixture.SceneOf(reopened);
        var node = scene.RootNodes.Single();
        SetPosition(fixture.Preview, node, 9);
        var current = fixture.Preview.CaptureRevision(scene);
        _ = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(9)], current, this.TestContext.CancellationToken).ConfigureAwait(false);

        var stale = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(1)], old, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = stale.Status.Should().Be(SyncStatus.Cancelled);
        _ = fixture.Preview.CaptureRevision(scene).DocumentLifetime.Should().Be(current.DocumentLifetime);
        _ = fixture.Preview.PendingFieldCount(reopened.Id).Should().Be(1);
        fixture.State = EngineServiceState.Running;
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Requests.Should().OnlyContain(request => request.Target.DocumentLifetime == current.DocumentLifetime);
        _ = fixture.Preview.PendingFieldCount(reopened.Id).Should().Be(0);
    }

    private static void SetPosition(ScenePreview preview, SceneNode node, float value)
    {
        node.Transform.LocalPosition = new Vector3(value, 0, 0);
        _ = preview.AdvanceRevision(node.Scene);
    }
}
