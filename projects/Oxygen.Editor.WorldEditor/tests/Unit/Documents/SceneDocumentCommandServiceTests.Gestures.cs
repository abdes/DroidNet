// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using AwesomeAssertions;
using DroidNet.Controls;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task CameraGesture_ComponentReplacementCancelsOriginalWithoutChangingReplacement()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CameraNode(scene, 60);
        var original = node.Components.OfType<PerspectiveCamera>().Single();
        var context = CreateContext(scene);
        var terminal = ConfigureGestureSync(fixture, scene);
        using var editor = new PerspectiveCameraViewModel(fixture.Sut, () => context);
        editor.UpdateValues([node]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.PointerDrag);
        editor.FieldOfView.Value = 90;

        _ = node.Components.Remove(original);
        var replacement = new PerspectiveCamera { Name = "Replacement", FieldOfView = 75 };
        node.Components.Add(replacement);
        editor.FieldOfView.Value = 100;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = original.FieldOfView.Should().Be(60);
        _ = replacement.FieldOfView.Should().Be(75);
        _ = editor.FieldOfView.Value.Should().Be(75);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = terminal.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CameraGesture_DuplicateTerminalCannotApplyAnotherValue()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CameraNode(scene, 60);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var token = EditSessionToken.Begin([node.Id], "FieldOfView");
        var edit = PropertyEdit.Single(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegrees, 90f);
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, new Dictionary<Guid, PropertyEdit> { [node.Id] = edit }, "Edit Camera", token).ConfigureAwait(false);
        token.Commit();
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, new Dictionary<Guid, PropertyEdit> { [node.Id] = PropertyEdit.Empty }, "Edit Camera", token).ConfigureAwait(false);

        var late = PropertyEdit.Single(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegrees, 100f);
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, new Dictionary<Guid, PropertyEdit> { [node.Id] = late }, "Edit Camera", token).ConfigureAwait(false);

        _ = node.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(90);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CameraOneShotPropertyEdit_WhenNodeIsLocked_IsRejected()
    {
        var scene = CreateScene();
        var node = CameraNode(scene, 60);
        var context = CreateContext(scene);
        var (interaction, _) = CreateInteraction();
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        interaction.SetLocked(node.Id, isLocked: true);
        var fixture = CreateFixture(interaction: interaction);
        var edit = PropertyEdit.Single(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegrees, 90f);

        var emptyResult = await fixture.Sut.EditPropertiesForTargetsAsync(
            context,
            new Dictionary<Guid, PropertyEdit> { [node.Id] = PropertyEdit.Empty },
            "Edit Camera",
            EditSessionToken.OneShot).ConfigureAwait(false);
        var result = await fixture.Sut.EditPropertiesForTargetsAsync(
            context,
            new Dictionary<Guid, PropertyEdit> { [node.Id] = edit },
            "Edit Camera",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = emptyResult.Succeeded.Should().BeTrue("empty completion notifications remain no-ops");
        _ = result.Succeeded.Should().BeFalse();
        _ = node.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(60);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "NODE_LOCKED");
    }

    [TestMethod]
    public async Task EnvironmentGesture_OneHundredSamplesCommitOnceAndUndoExactly()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var terminal = ConfigureGestureSync(fixture, scene);
        var original = scene.Environment.BackgroundColor;
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);

        for (var sample = 1; sample <= 100; ++sample)
        {
            editor.Background.BackgroundR = sample / 100f;
        }

        _ = context.History.UndoStack.Should().BeEmpty();
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = terminal.Should().ContainSingle();
        _ = scene.Environment.BackgroundColor.Should().Be(new Vector3(1, original.Y, original.Z));

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Environment.BackgroundColor.Should().Be(original);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Environment.BackgroundColor.Should().Be(new Vector3(1, original.Y, original.Z));
    }

    [TestMethod]
    public async Task ExposureShapingDescriptors_EditThroughCanonicalSceneTransactionAndUndo()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var before = scene.Environment.PostProcess;
        var curve = ImmutableArray.Create(new ExposureCompensationKeyData(-4, 1), new ExposureCompensationKeyData(12, -0.5f));
        var mask = new Uri("asset:///Content/Textures/Meter.otex.json");
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureBlackInfluence, 0.35f);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureTransitionDistanceEv, 2.5f);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMask, mask);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureCompensationCurve, curve);

        var result = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            edit,
            "Edit auto exposure",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.PostProcess.AutoExposureBlackInfluence.Should().Be(0.35f);
        _ = scene.Environment.PostProcess.AutoExposureTransitionDistanceEv.Should().Be(2.5f);
        _ = scene.Environment.PostProcess.AutoExposureMeteringMask.Should().Be(mask);
        _ = scene.Environment.PostProcess.AutoExposureCompensationCurve.Should().Equal(curve);
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Environment.PostProcess.Should().BeEquivalentTo(before);
    }

    [TestMethod]
    public async Task WheelGesture_CommitsAfterIdleAndNotAfterEachTick()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CameraNode(scene, 60);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new PerspectiveCameraViewModel(fixture.Sut, () => context);
        editor.UpdateValues([node]);
        for (var tick = 0; tick < 4; ++tick)
        {
            editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.MouseWheel);
            editor.FieldOfView.Value = 61 + tick;
            editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit));
        }

        _ = context.History.UndoStack.Should().BeEmpty();
        await editor.PendingEdits.ConfigureAwait(false);

        _ = context.History.UndoStack.Should().ContainSingle();
        _ = node.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(64);
    }

    [TestMethod]
    public async Task RejectedCameraPreview_RestoresDisplayedCommittedValue()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CameraNode(scene, 60);
        var camera = node.Components.OfType<PerspectiveCamera>().Single();
        camera.NearPlane = 1;
        camera.FarPlane = 10;
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new PerspectiveCameraViewModel(fixture.Sut, () => context);
        editor.UpdateValues([node]);
        editor.BeginEditSession("NearPlane", NumberBoxEditInteractionKind.Text);
        editor.NearPlane.Value = 11;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = camera.NearPlane.Should().Be(1);
        _ = editor.NearPlane.Value.Should().Be(1);
        _ = editor.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = editor.FarPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        editor.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await editor.PendingEdits.ConfigureAwait(false);
        editor.BeginEditSession("FarPlane", NumberBoxEditInteractionKind.Text);
        editor.FarPlane.Value = 20;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);
        _ = editor.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = editor.FarPlaneDiagnostic.Message.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CameraGesture_OneHundredSamplesCommitOneHistoryEntryAndRestoreEachTarget()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CameraNode(scene, 60);
        var second = CameraNode(scene, 80);
        var context = CreateContext(scene);
        var terminal = ConfigureGestureSync(fixture, scene);
        using var editor = new PerspectiveCameraViewModel(fixture.Sut, () => context);
        editor.UpdateValues([first, second]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.PointerDrag);

        for (var value = 1; value <= 100; ++value)
        {
            editor.FieldOfView.Value = value;
        }

        _ = context.History.UndoStack.Should().BeEmpty();
        editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit));
        await editor.PendingEdits.ConfigureAwait(false);

        _ = context.History.UndoStack.Should().ContainSingle();
        _ = terminal.Should().HaveCount(2, "one terminal projection is issued for each target");
        _ = first.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(100);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(60);
        _ = second.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(80);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = second.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(100);
    }

    [TestMethod]
    public async Task CameraGesture_CancelAndSelectionChangeRejectLateSamples()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CameraNode(scene, 60);
        var second = CameraNode(scene, 80);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new PerspectiveCameraViewModel(fixture.Sut, () => context);
        editor.UpdateValues([first]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.PointerDrag);
        editor.FieldOfView.Value = 110;

        editor.UpdateValues([second]);
        editor.FieldOfView.Value = 120;
        editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Cancel));
        await editor.PendingEdits.ConfigureAwait(false);

        _ = first.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(60);
        _ = second.Components.OfType<PerspectiveCamera>().Single().FieldOfView.Should().Be(80);
        _ = editor.FieldOfView.Value.Should().Be(80);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task LightColorAxis_PreservesUneditedChannelsAndGroupsUndo()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CreateDirectionalLightNode(scene, "First");
        var second = CreateDirectionalLightNode(scene, "Second");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var firstLight = first.Components.OfType<DirectionalLightComponent>().Single();
        var secondLight = second.Components.OfType<DirectionalLightComponent>().Single();
        firstLight.Color = new Vector3(0.1f, 0.2f, 0.3f);
        secondLight.Color = new Vector3(0.4f, 0.5f, 0.6f);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new DirectionalLightViewModel(fixture.Sut, () => context);
        editor.UpdateValues([first, second]);
        editor.BeginEditSession("ColorR", NumberBoxEditInteractionKind.PointerDrag);
        editor.ColorR = 0.7f;
        editor.ColorR = 0.8f;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = firstLight.Color.Should().Be(new Vector3(0.8f, 0.2f, 0.3f));
        _ = secondLight.Color.Should().Be(new Vector3(0.8f, 0.5f, 0.6f));
        _ = context.History.UndoStack.Should().ContainSingle();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = firstLight.Color.Should().Be(new Vector3(0.1f, 0.2f, 0.3f));
        _ = secondLight.Color.Should().Be(new Vector3(0.4f, 0.5f, 0.6f));
    }

    [TestMethod]
    public async Task EnvironmentGesture_ChangesPreviewThenCancelsWithoutHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var original = scene.Environment.BackgroundColor;
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        editor.Background.BackgroundR = 0.8f;
        _ = scene.Environment.BackgroundColor.X.Should().Be(0.8f);

        editor.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = scene.Environment.BackgroundColor.Should().Be(original);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
    }

    private static SceneNode CameraNode(Scene scene, float fieldOfView)
    {
        var node = new SceneNode(scene) { Name = "Camera" };
        node.Components.Add(new PerspectiveCamera { Name = "Camera", FieldOfView = fieldOfView });
        scene.RootNodes.Add(node);
        return node;
    }

    private static List<Guid> ConfigureGestureSync(Fixture fixture, Scene scene)
    {
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty);
        var terminal = new List<Guid>();
        _ = fixture.Sync.Setup(value => value.UpdatePropertiesAsync(scene, It.IsAny<SceneNode>(), It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = fixture.Sync.Setup(value => value.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)));
        _ = fixture.Sync.Setup(value => value.TryPreviewSyncAsync(scene.Id, It.IsAny<Guid>(), It.IsAny<DateTimeOffset>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
            .Returns((Guid _, Guid _, DateTimeOffset _, Func<CancellationToken, Task<SyncOutcome>> sync, CancellationToken cancellationToken) => Preview(sync, cancellationToken));
        _ = fixture.Sync.Setup(value => value.CompleteTerminalSyncAsync(scene.Id, It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
            .Returns((Guid _, Guid node, Func<CancellationToken, Task<SyncOutcome>> sync, CancellationToken cancellationToken) =>
            {
                terminal.Add(node);
                return sync(cancellationToken);
            });
        _ = fixture.Sync.Setup(value => value.CancelPreviewSyncAsync(scene.Id, It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
            .Returns((Guid _, Guid _, Func<CancellationToken, Task<SyncOutcome>> sync, CancellationToken cancellationToken) => sync(cancellationToken));
        return terminal;

        static async Task<SyncOutcome?> Preview(Func<CancellationToken, Task<SyncOutcome>> sync, CancellationToken cancellationToken)
            => await sync(cancellationToken).ConfigureAwait(false);
    }
}
