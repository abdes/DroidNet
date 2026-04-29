// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Core.Diagnostics;
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
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateNodeTransformAsync(scene, node);

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
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateNodeTransformAsync(scene, node);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
    }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        using var cts = new CancellationTokenSource();
        cts.Cancel();

        var outcome = await sut.UpdateNodeTransformAsync(scene, node, cts.Token);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task UpdateMaterialSlot_WhenEngineFaulted_ReturnsRuntimeFaulted()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Faulted);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await sut.UpdateMaterialSlotAsync(
            scene,
            node,
            slotIndex: 0,
            materialUri: new Uri("asset:///Materials/Test"));

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.RuntimeFaulted);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateMaterialSlot_WhenCancelled_ReturnsCancelled()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        using var cts = new CancellationTokenSource();
        cts.Cancel();

        var outcome = await sut.UpdateMaterialSlotAsync(
            scene,
            node,
            slotIndex: 0,
            materialUri: null,
            cancellationToken: cts.Token);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task AttachCamera_WhenEngineNotRunning_ReturnsSkippedBeforeUnsupportedCamera()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Camera" };
        node.Components.Add(new OrthographicCamera { Name = "Camera" });

        var outcome = await sut.AttachCameraAsync(scene, node);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        engine.VerifyGet(s => s.World, Times.Never);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        engine.SetupGet(s => s.State).Returns(EngineServiceState.Ready);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment);

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
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment);

        _ = result.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = result.PerField.Should().BeEmpty();
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
        var scene = CreateScene();
        using var cts = new CancellationTokenSource();
        cts.Cancel();

        var result = await sut.UpdateEnvironmentAsync(scene, scene.Environment, cts.Token);

        _ = result.Overall.Should().Be(SyncStatus.Cancelled);
        _ = result.PerField.Should().BeEmpty();
        engine.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task MaterialOverrideLegacyMethods_DoNotThrowWhenEngineApiIsUnsupported()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);

        await sut.UpdateMaterialOverrideAsync(Guid.NewGuid(), new MaterialsSlot());
        await sut.UpdateTargetedMaterialOverrideAsync(Guid.NewGuid(), lodIndex: 0, submeshIndex: 0, new MaterialsSlot());
        await sut.RemoveMaterialOverrideAsync(Guid.NewGuid(), typeof(MaterialsSlot));
        await sut.RemoveTargetedMaterialOverrideAsync(Guid.NewGuid(), lodIndex: 0, submeshIndex: 0, typeof(MaterialsSlot));
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
        var sut = new SceneEngineSync(engine.Object, NullLoggerFactory.Instance);
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
                });

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
            });

        _ = terminal.Should().Be(accepted);
        _ = terminalCount.Should().Be(1);
        _ = sut.ShouldIssuePreviewSync(sceneId, nodeId, start.AddMilliseconds(100)).Should().BeTrue();
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }
}
