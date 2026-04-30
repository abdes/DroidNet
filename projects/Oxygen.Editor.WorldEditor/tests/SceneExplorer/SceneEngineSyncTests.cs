// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
[TestCategory("Live Engine Sync")]
public sealed class SceneEngineSyncTests
{
    [TestMethod]
    public async Task UpdateNodeTransform_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateNodeTransformAsync(scene, node).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = outcome.Scope.SceneId.Should().Be(scene.Id);
        _ = outcome.Scope.NodeId.Should().Be(node.Id);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenWorldIsNull_ReturnsSkippedNotRunning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Running);
        engine.SetupGet(s => s.World).Returns((Oxygen.Interop.World.OxygenWorld)null!);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateNodeTransformAsync(scene, node).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
    }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);

        var outcome = await sut.UpdateNodeTransformAsync(scene, node, cts.Token).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task UpdateProperties_WhenEngineNotRunning_BuffersPendingPropertySync()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        var entries = new[] { CreateTransformEntry(42.0f) };
        var countChanges = new List<PendingPropertySyncCountChangedEventArgs>();
        sut.PendingPropertySyncCountChanged += (_, args) => countChanges.Add(args);

        var outcome = await sut.UpdatePropertiesAsync(scene, node, entries).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = outcome.Message.Should().Contain("1 pending property edit");
        _ = sut.GetPendingPropertySyncCount(scene.Id).Should().Be(1);
        _ = countChanges.Should().ContainSingle()
            .Which.Should().Match<PendingPropertySyncCountChangedEventArgs>(
                args => args.SceneId == scene.Id && args.PendingCount == 1);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateProperties_WhenCancelled_DoesNotBufferOrReadEngineState()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        var entries = new[] { CreateTransformEntry(42.0f) };
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);

        var outcome = await sut.UpdatePropertiesAsync(scene, node, entries, cts.Token).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        _ = sut.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task UpdateProperties_WhenEngineFaulted_DoesNotBuffer()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Faulted);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        var entries = new[] { CreateTransformEntry(42.0f) };

        var outcome = await sut.UpdatePropertiesAsync(scene, node, entries).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.RuntimeFaulted);
        _ = sut.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateMaterialSlot_WhenEngineFaulted_ReturnsRuntimeFaulted()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Faulted);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateMaterialSlotAsync(
            scene,
            node,
            slotIndex: 0,
            materialUri: new Uri("asset:///Materials/Test")).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.RuntimeFaulted);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateMaterialSlot_WhenCancelled_ReturnsCancelled()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);

        var outcome = await sut.UpdateMaterialSlotAsync(
            scene,
            node,
            slotIndex: 0,
            materialUri: null,
            cancellationToken: cts.Token).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task AttachCamera_WhenEngineNotRunning_ReturnsSkippedBeforeUnsupportedCamera()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Camera" };
        node.Components.Add(new OrthographicCamera { Name = "Camera" });

        var outcome = await sut.AttachCameraAsync(scene, node).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = result.PerField.Should().BeEmpty();
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenRunningWorldIsMissing_ReturnsSkippedNotRunning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Running);
        engine.SetupGet(s => s.World).Returns((Oxygen.Interop.World.OxygenWorld)null!);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = result.PerField.Should().BeEmpty();
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment, cts.Token).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.Cancelled);
        _ = result.PerField.Should().BeEmpty();
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task MaterialOverrideLegacyMethods_DoNotThrowWhenEngineApiIsUnsupported()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);

        await sut.UpdateMaterialOverrideAsync(Guid.NewGuid(), new MaterialsSlot()).ConfigureAwait(false);
        await sut.UpdateTargetedMaterialOverrideAsync(Guid.NewGuid(), lodIndex: 0, submeshIndex: 0, new MaterialsSlot()).ConfigureAwait(false);
        await sut.RemoveMaterialOverrideAsync(Guid.NewGuid(), typeof(MaterialsSlot)).ConfigureAwait(false);
        await sut.RemoveTargetedMaterialOverrideAsync(Guid.NewGuid(), lodIndex: 0, submeshIndex: 0, typeof(MaterialsSlot)).ConfigureAwait(false);
    }

    [TestMethod]
    public void MaterialOverridePathMapper_MapsDescriptorUriToCookedEnginePath()
    {
        var path = MaterialOverridePathMapper.ToEnginePath(new Uri("asset:///Content/Materials/Red.omat.json"));

        _ = path.Should().Be("/Content/Materials/Red.omat");
    }

    [TestMethod]
    public void MaterialOverridePathMapper_MapsNullAndEmptySentinelToClearOverride()
    {
        _ = MaterialOverridePathMapper.ToEnginePath(null).Should().BeNull();
        _ = MaterialOverridePathMapper.ToEnginePath(new Uri("asset:///__uninitialized__")).Should().BeNull();
    }

    [TestMethod]
    public void GeometryMaterialDisplayName_StripsDescriptorAndCookedExtensions()
    {
        _ = GeometryViewModel.ExtractMaterialNameFromUriString("asset:///Content/Materials/Red.omat.json")
            .Should().Be("Red");
        _ = GeometryViewModel.ExtractMaterialNameFromUriString("asset:///Content/Materials/Blue.omat")
            .Should().Be("Blue");
    }

    [TestMethod]
    public async Task Coalescer_ThrottlesPreviewAndAllowsOneTerminalSyncThroughSyncService()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        using var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var sceneId = Guid.NewGuid();
        var nodeId = Guid.NewGuid();
        var start = DateTimeOffset.Parse("2026-04-27T00:00:00Z", null, System.Globalization.DateTimeStyles.AssumeUniversal);
        var previewCount = 0;
        var terminalCount = 0;
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty);

        for (var i = 0; i < 100; i++)
        {
            var outcome = await sut.TryPreviewSyncAsync(
                sceneId,
                nodeId,
                start.AddMilliseconds(i),
                _ =>
                {
                    previewCount++;
                    return Task.FromResult(accepted);
                }).ConfigureAwait(false);

            if (outcome is not null)
            {
                _ = outcome.Should().Be(accepted);
            }
        }

        _ = previewCount.Should().BeLessThanOrEqualTo(7);
        var terminal = await sut.CompleteTerminalSyncAsync(
            sceneId,
            nodeId,
            _ =>
            {
                terminalCount++;
                return Task.FromResult(accepted);
            }).ConfigureAwait(false);

        _ = terminal.Should().Be(accepted);
        _ = terminalCount.Should().Be(1);
        _ = sut.ShouldIssuePreviewSync(sceneId, nodeId, start.AddMilliseconds(100)).Should().BeTrue();
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }

    private static EnginePropertyValueEntry CreateTransformEntry(float value)
        => new(EngineComponentId.Transform, (ushort)TransformField.PositionX, value);
}
