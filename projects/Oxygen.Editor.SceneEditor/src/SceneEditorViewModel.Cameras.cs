// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor;

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

    /// <summary>Runs a camera command in the focused viewport, while this editor is the window's active one.</summary>
    /// <param name="nodeId">The camera node.</param>
    /// <param name="command">The command.</param>
    /// <returns>Whether the command ran.</returns>
    internal Task<bool> RunCameraCommandAsync(NodeId nodeId, SceneCameraCommand command)
        => !this.isDisposed && this.IsActiveDocument()
            ? this.ExecuteCameraCommandAsync(nodeId, command)
            : Task.FromResult(false);

    /// <summary>Gets what the focused viewport shows, while this editor is the window's active one.</summary>
    /// <returns>The viewport's camera state, or <see langword="null"/> when this editor has no viewport to ask.</returns>
    internal SceneCameraViewState? CameraViewState()
        => !this.isDisposed && this.IsActiveDocument() && this.GetActiveViewport() is { } viewport
            ? new SceneCameraViewState(viewport.SceneCamera?.NodeId, viewport.IsPilotingSceneCamera)
            : null;

    private SceneNodeInteraction Interaction => this.services.Interaction;

    private void AttachCameraServices(ViewportViewModel viewport)
    {
        viewport.SceneCamerasProvider = this.GetSceneCameras;
        viewport.SelectedCameraProvider = this.GetSelectedCamera;
        viewport.CameraLockProvider = this.IsCameraLocked;
        viewport.CameraPoseCommitter = this.CommitCameraPoseAsync;
    }

    // Selecting one camera node previews it in the focused pane; the selection also scopes Browse to Asset.
    private void OnSelectionServiceChanged(object? sender, SceneSelectionChangedEventArgs args)
    {
        _ = sender;
        if (this.isDisposed || args.DocumentId != this.Document.Id || this.scene is not { } selectedScene)
        {
            return;
        }

        var nodes = this.SelectionService.GetSelectedNodes(args.DocumentId, selectedScene);
        this.UpdateSelectedNodes(nodes);
        this.UpdateSelectionOutline(args.Context);
        this.UpdateTransformGizmo();
        this.selectedCamera = nodes is [{ } node] && node.Components.OfType<CameraComponent>().Any()
            ? new SceneCameraChoice(node.Id, node.Metadata.Name)
            : null;
        this.UpdateInsets();
    }

    private bool IsActiveDocument()
        => ReferenceEquals(this.window?.Area.ActiveDocument, this.Document);

    private ViewportViewModel? GetActiveViewport()
        => this.Viewports.FirstOrDefault(viewport => viewport.ViewportId == this.FocusedViewportId)
            ?? this.Viewports.FirstOrDefault();

    private async Task<bool> ExecuteCameraCommandAsync(NodeId nodeId, SceneCameraCommand command)
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
                .Select(static node => new SceneCameraChoice(node.Id, node.Metadata.Name))];

    private SceneCameraChoice? GetSelectedCamera()
    {
        return this.scene is { } current
            && this.SelectionService.GetSelectedNodes(this.Document.Id, current) is [{ } node]
            && node.Components.OfType<CameraComponent>().Any()
                ? new SceneCameraChoice(node.Id, node.Metadata.Name)
                : null;
    }

    private bool IsCameraLocked(NodeId nodeId)
        => this.FindNode(nodeId) is { } node
            && this.Interaction.GetLockOwner(node) is not null;

    private SceneNode? FindNode(NodeId nodeId)
        => this.scene?.AllNodes.FirstOrDefault(node => node.Id == nodeId);

    /// <summary>
    /// Records a pose read from a viewport as one undoable step on the camera node: its transform, and
    /// the view's orthographic size or field of view when those changed. Unchanged values record nothing.
    /// </summary>
    private async Task<bool> CommitCameraPoseAsync(NodeId nodeId, RuntimeViewCameraPose pose)
    {
        if (this.scene is null || this.FindNode(nodeId) is not { } node)
        {
            return false;
        }

        var edits = new List<ISceneEdit>();
        if (node.Transform is not { } transform
            || !HasPose(transform, pose))
        {
            var values = new PropertyEdit.Builder()
                .Set(SceneSchema.Transform.PositionX, pose.Position.X)
                .Set(SceneSchema.Transform.PositionY, pose.Position.Y)
                .Set(SceneSchema.Transform.PositionZ, pose.Position.Z)
                .Set(SceneSchema.Transform.RotationX, pose.RotationEulerDegrees.X)
                .Set(SceneSchema.Transform.RotationY, pose.RotationEulerDegrees.Y)
                .Set(SceneSchema.Transform.RotationZ, pose.RotationEulerDegrees.Z)
                .Build();
            edits.Add(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [nodeId] = values }, "Move Camera"));
        }

        if (pose.OrthographicSize is { } size
            && node.Components.OfType<OrthographicCamera>().FirstOrDefault() is { } orthographic
            && Math.Abs(orthographic.OrthographicSize - size) > SizeTolerance)
        {
            var values = PropertyEdit.SingleEdit(SceneSchema.OrthographicCamera.OrthographicSize, size);
            edits.Add(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [nodeId] = values }, "Edit Orthographic Size"));
        }

        if (pose.FieldOfViewDegrees is { } fieldOfView
            && node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } perspective
            && Math.Abs(perspective.FieldOfView - fieldOfView) > FieldOfViewTolerance)
        {
            var values = PropertyEdit.SingleEdit(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, fieldOfView);
            edits.Add(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [nodeId] = values }, "Edit Field of View"));
        }

        if (edits.Count == 0 || this.Authoring is not { } authoring)
        {
            return false;
        }

        // One undo step however many properties the view changed.
        var edit = edits.Count == 1 ? edits[0] : new SceneEditBatch($"Move camera '{node.Metadata.Name}'", edits);
        return (await authoring.ExecuteAsync(edit).ConfigureAwait(true)).Succeeded;
    }

    private static bool HasPose(TransformComponent transform, RuntimeViewCameraPose pose)
    {
        var rotation = TransformConverter.EulerDegreesToQuaternion(pose.RotationEulerDegrees);
        return Vector3.Distance(transform.LocalPosition, pose.Position) <= PositionTolerance
            && Math.Abs(Quaternion.Dot(Quaternion.Normalize(transform.LocalRotation), Quaternion.Normalize(rotation))) >= 1.0F - RotationDotTolerance;
    }
}
