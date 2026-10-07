// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Scene-camera viewing for the scene's viewports: which cameras exist, which one is selected,
/// committing a pose read from a viewport as undoable edits, and camera actions requested by
/// other panes (Scene Explorer) for the active viewport.
/// </summary>
public partial class SceneEditorViewModel
{
    private const float PositionTolerance = 1.0e-4F;
    private const float RotationDotTolerance = 1.0e-6F;
    private const float SizeTolerance = 1.0e-4F;
    private const float FieldOfViewTolerance = 1.0e-3F;

    private Workspace.WorkspaceInteractionService? Interaction
        => this.container.Resolve<Workspace.WorkspaceInteractionService>(IfUnresolved.ReturnDefault);

    private void AttachCameraServices(ViewportViewModel viewport)
    {
        viewport.SceneCamerasProvider = this.GetSceneCameras;
        viewport.SelectedCameraProvider = this.GetSelectedCamera;
        viewport.CameraLockProvider = this.IsCameraLocked;
        viewport.CameraPoseCommitter = this.CommitCameraPoseAsync;
    }

    private void RegisterCameraMessages()
    {
        this.messenger.Register<SceneCameraViewportStateRequestMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId && this.IsActiveDocument()
                && !message.HasReceivedResponse && this.GetActiveViewport() is { } viewport)
            {
                message.Reply(new SceneCameraViewportState(viewport.SceneCamera?.NodeId, viewport.IsPilotingSceneCamera));
            }
        });
        this.messenger.Register<SceneCameraCommandMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId && this.IsActiveDocument()
                && !message.HasReceivedResponse)
            {
                message.Reply(this.ExecuteCameraCommandAsync(message.NodeId, message.Command));
            }
        });
    }

    private bool IsActiveDocument()
        => this.documentService.GetActiveDocumentId(this.windowId) == this.Metadata.DocumentId;

    private ViewportViewModel? GetActiveViewport()
        => this.Viewports.FirstOrDefault(viewport => viewport.ViewportId == this.FocusedViewportId)
            ?? this.Viewports.FirstOrDefault();

    private async Task<bool> ExecuteCameraCommandAsync(Guid nodeId, SceneCameraCommand command)
    {
        if (this.GetActiveViewport() is not { } viewport
            || this.GetSceneCameras().FirstOrDefault(camera => camera.NodeId == nodeId) is not { } camera)
        {
            return false;
        }

        switch (command)
        {
            case SceneCameraCommand.LookThrough:
                await viewport.LookThroughCameraAsync(camera).ConfigureAwait(true);
                return true;
            case SceneCameraCommand.ReturnToEditorCamera:
                await viewport.ReturnToEditorCameraAsync().ConfigureAwait(true);
                return true;
            case SceneCameraCommand.Pilot:
                if (this.IsCameraLocked(nodeId))
                {
                    return false;
                }

                await viewport.PilotCameraAsync(camera).ConfigureAwait(true);
                return true;
            case SceneCameraCommand.StopPiloting:
                await viewport.StopPilotingAsync().ConfigureAwait(true);
                return true;
            case SceneCameraCommand.AlignToView:
                return await viewport.AlignCameraToViewAsync(camera).ConfigureAwait(true);
            default:
                return false;
        }
    }

    private IReadOnlyList<SceneCameraChoice> GetSceneCameras()
        => this.scene is null
            ? []
            : [.. this.scene.AllNodes
                .Where(static node => node.Components.OfType<CameraComponent>().Any())
                .Select(static node => new SceneCameraChoice(node.Id, node.Name))];

    private SceneCameraChoice? GetSelectedCamera()
    {
        var request = this.messenger.Send(new SceneNodeSelectionRequestMessage());
        return request.HasReceivedResponse
            && request.SelectedEntities is [{ } node]
            && node.Components.OfType<CameraComponent>().Any()
                ? new SceneCameraChoice(node.Id, node.Name)
                : null;
    }

    private bool IsCameraLocked(Guid nodeId)
        => this.Interaction is { } interaction
            && this.FindNode(nodeId) is { } node
            && interaction.GetLockOwner(node) is not null;

    private SceneNode? FindNode(Guid nodeId)
        => this.scene?.AllNodes.FirstOrDefault(node => node.Id == nodeId);

    /// <summary>
    /// Records a pose read from a viewport as one undoable step on the camera node: its transform, and
    /// the view's orthographic size or field of view when those changed. Unchanged values record nothing.
    /// </summary>
    private async Task<bool> CommitCameraPoseAsync(Guid nodeId, RuntimeViewCameraPose pose)
    {
        if (this.scene is null || this.FindNode(nodeId) is not { } node)
        {
            return false;
        }

        var edits = new List<Func<SceneDocumentCommandContext, Task<SceneCommandResult>>>();
        if (node.Components.OfType<TransformComponent>().FirstOrDefault() is not { } transform
            || !HasPose(transform, pose))
        {
            var edit = new TransformEdit(
                Position: new OptionalEditValue<Vector3>(pose.Position),
                RotationEulerDegrees: new OptionalEditValue<Vector3>(pose.RotationEulerDegrees),
                Scale: default);
            edits.Add(context => this.commandService.EditTransformAsync(context, [nodeId], edit, EditSessionToken.OneShot));
        }

        if (pose.OrthographicSize is { } size
            && node.Components.OfType<OrthographicCamera>().FirstOrDefault() is { } orthographic
            && Math.Abs(orthographic.OrthographicSize - size) > SizeTolerance)
        {
            edits.Add(context => this.commandService.EditPropertiesAsync(
                context,
                [nodeId],
                PropertyEdit.SingleEdit(SceneDocumentCommandService.OrthographicCamera.OrthographicSize, size),
                "Edit Orthographic Size",
                EditSessionToken.OneShot));
        }

        if (pose.FieldOfViewDegrees is { } fieldOfView
            && node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } perspective
            && Math.Abs(perspective.FieldOfView - fieldOfView) > FieldOfViewTolerance)
        {
            edits.Add(context => this.commandService.EditPropertiesAsync(
                context,
                [nodeId],
                PropertyEdit.SingleEdit(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegrees, fieldOfView),
                "Edit Field of View",
                EditSessionToken.OneShot));
        }

        if (edits.Count == 0)
        {
            return false;
        }

        var commandContext = this.CreateCommandContext();
        var grouped = edits.Count > 1;
        if (grouped)
        {
            // One undo step however many properties the view changed.
            commandContext.History.BeginChangeSet($"Move camera '{node.Name}'");
        }

        try
        {
            var applied = false;
            foreach (var edit in edits)
            {
                applied |= (await edit(commandContext).ConfigureAwait(true)).Succeeded;
            }

            return applied;
        }
        finally
        {
            if (grouped)
            {
                commandContext.History.EndChangeSet();
            }
        }
    }

    private static bool HasPose(TransformComponent transform, RuntimeViewCameraPose pose)
    {
        var rotation = TransformConverter.EulerDegreesToQuaternion(pose.RotationEulerDegrees);
        return Vector3.Distance(transform.LocalPosition, pose.Position) <= PositionTolerance
            && Math.Abs(Quaternion.Dot(Quaternion.Normalize(transform.LocalRotation), Quaternion.Normalize(rotation))) >= 1.0F - RotationDotTolerance;
    }
}
