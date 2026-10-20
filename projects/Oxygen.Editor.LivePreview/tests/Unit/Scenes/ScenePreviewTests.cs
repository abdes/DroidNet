// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Interop;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

[TestClass]
[TestCategory("Scene Preview")]
public sealed partial class ScenePreviewTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var scene = new Scene { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };
        var reads = fixture.Engine.WorldReads;

        var outcome = await fixture.Preview.UpdateNodeTransformAsync(scene, node, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = outcome.Scope.SceneId.Should().Be(scene.Id);
        _ = outcome.Scope.NodeId.Should().Be(node.Id);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenWorldIsNull_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture();
        fixture.Engine.HasWorld = false;
        var scene = new Scene { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };

        var outcome = await fixture.Preview.UpdateNodeTransformAsync(scene, node, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
    }

    [TestMethod]
    public async Task UpdateNodeTransform_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        using var fixture = new PreviewFixture();
        var scene = new Scene { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);
        var (states, worlds) = (fixture.Engine.StateReads, fixture.Engine.WorldReads);

        var outcome = await fixture.Preview.UpdateNodeTransformAsync(scene, node, cts.Token).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        _ = (fixture.Engine.StateReads, fixture.Engine.WorldReads).Should().Be((states, worlds));
    }

    [TestMethod]
    public async Task RenameNode_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var scene = new Scene { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };
        var reads = fixture.Engine.WorldReads;

        var outcome = await fixture.Preview.RenameNodeAsync(scene, node.Id, "Sphere").ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = outcome.OperationKind.Should().Be(SceneOperationKinds.NodeRename);
        _ = outcome.Scope.NodeId.Should().Be(node.Id);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task UpdateProperties_WhenEngineNotRunning_BuffersPendingFields()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var changes = new List<ScenePreviewEventArgs>();
        fixture.Preview.PendingFieldsChanged += (_, args) => changes.Add(args);
        var reads = fixture.Engine.WorldReads;

        var outcome = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(42.0f)], cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(1);
        _ = changes.Should().ContainSingle().Which.Document.Should().Be(document.Id);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task UpdateProperties_WhenCancelled_DoesNotBufferOrReadEngineState()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);
        var (states, worlds) = (fixture.Engine.StateReads, fixture.Engine.WorldReads);

        var outcome = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(42.0f)], cts.Token).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.Cancelled);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.Cancelled);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = (fixture.Engine.StateReads, fixture.Engine.WorldReads).Should().Be((states, worlds));
    }

    [TestMethod]
    public async Task UpdateProperties_WhenEngineFaulted_BuffersForRecovery()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Faulted);
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var reads = fixture.Engine.WorldReads;

        var outcome = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(42.0f)], cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.RuntimeFaulted);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(1);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task AttachOrthographicCamera_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var scene = new Scene { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Camera" };
        node.Components.Add(new OrthographicCamera { Name = "Camera" });
        var reads = fixture.Engine.WorldReads;

        var outcome = await fixture.Preview.AttachCameraAsync(scene, node, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(SyncStatus.SkippedNotRunning);
        _ = outcome.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenEngineNotRunning_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (scene, _, _) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var reads = fixture.Engine.WorldReads;

        var result = await fixture.Preview.UpdateEnvironmentAsync(scene, scene.Environment, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = result.PerField.Should().NotBeEmpty();
        _ = result.PerField.Values.Should().OnlyContain(outcome => outcome.Status == SyncStatus.SkippedNotRunning);
        _ = fixture.Engine.WorldReads.Should().Be(reads);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenRunningWorldIsMissing_ReturnsSkippedNotRunning()
    {
        using var fixture = new PreviewFixture();
        fixture.Engine.HasWorld = false;
        var (scene, _, _) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);

        var result = await fixture.Preview.UpdateEnvironmentAsync(scene, scene.Environment, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.SkippedNotRunning);
        _ = result.PerField.Should().NotBeEmpty();
        _ = result.PerField.Values.Should().OnlyContain(outcome => outcome.Status == SyncStatus.SkippedNotRunning);
    }

    [TestMethod]
    public async Task UpdateEnvironment_WhenCancelled_ReturnsCancelledWithoutReadingEngineState()
    {
        using var fixture = new PreviewFixture();
        var scene = new Scene { Name = "Test Scene" };
        using var cts = new CancellationTokenSource();
        await cts.CancelAsync().ConfigureAwait(false);
        var (states, worlds) = (fixture.Engine.StateReads, fixture.Engine.WorldReads);

        var result = await fixture.Preview.UpdateEnvironmentAsync(scene, scene.Environment, cts.Token).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.Cancelled);
        _ = result.PerField.Should().NotBeEmpty();
        _ = result.PerField.Values.Should().OnlyContain(outcome => outcome.Status == SyncStatus.Cancelled);
        _ = (fixture.Engine.StateReads, fixture.Engine.WorldReads).Should().Be((states, worlds));
    }

    [TestMethod]
    public void MaterialOverridePathMapper_MapsIdentityToEnginePath()
        => _ = MaterialOverridePathMapper.ToEnginePath(AssetPath.Parse("/Content/Materials/Red.omat")).Should().Be("/Content/Materials/Red.omat");

    [TestMethod]
    public void MaterialOverridePathMapper_ClearsOnlyAnExplicitNull()
        => _ = MaterialOverridePathMapper.ToEnginePath(material: null).Should().BeNull();

    [TestMethod]
    public async Task Coalescer_ThrottlesPreviewAndAllowsOneTerminalSync()
    {
        using var fixture = new PreviewFixture();
        var sceneId = SceneId.New();
        var nodeId = Guid.NewGuid();
        var start = DateTimeOffset.Parse("2026-04-27T00:00:00Z", System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.AssumeUniversal);
        var previewCount = 0;
        var terminalCount = 0;
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty);

        for (var i = 0; i < 100; i++)
        {
            var outcome = await fixture.Preview.TryPreviewSyncAsync(
                sceneId,
                nodeId,
                start.AddMilliseconds(i),
                _ =>
                {
                    previewCount++;
                    return Task.FromResult(accepted);
                },
                cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

            if (outcome is not null)
            {
                _ = outcome.Should().Be(accepted);
            }
        }

        _ = previewCount.Should().BeLessThanOrEqualTo(7);
        var terminal = await fixture.Preview.CompleteTerminalSyncAsync(
            sceneId,
            nodeId,
            _ =>
            {
                terminalCount++;
                return Task.FromResult(accepted);
            },
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = terminal.Should().Be(accepted);
        _ = terminalCount.Should().Be(1);
        _ = fixture.Preview.ShouldIssuePreviewSync(sceneId, nodeId, start.AddMilliseconds(100)).Should().BeTrue();
    }

    private static EnginePropertyValueEntry CreateTransformEntry(float value)
        => new(RuntimeComponentId.Transform, (ushort)TransformField.PositionX, value);

    private static RuntimePropertyValue PositionX(float value)
        => new(RuntimeComponentId.Transform, (ushort)TransformField.PositionX, value);
}
