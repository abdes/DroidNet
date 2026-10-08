// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts managed view requests at the native session boundary.</summary>
internal sealed partial class NativeEngineSession
{
    /// <inheritdoc />
    public override async Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config)
    {
        var result = await this.Runner.TryCreateViewAsync(this.context, NativeSessionConversions.ToNative(config)).ConfigureAwait(false);
        return new(result.Value);
    }

    /// <inheritdoc />
    public override Task<bool> DestroyViewAsync(RuntimeViewId viewId) => this.Runner.TryDestroyViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> ShowViewAsync(RuntimeViewId viewId) => this.Runner.TryShowViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> HideViewAsync(RuntimeViewId viewId) => this.Runner.TryHideViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset)
        => this.Runner.TrySetViewCameraPresetAsync(this.context, new(viewId.Value), NativeSessionConversions.ToNative<CameraViewPresetManaged>(preset));

    /// <inheritdoc />
    public override Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, Guid? cameraNodeId)
        => this.Runner.TrySetViewSceneCameraAsync(this.context, new(viewId.Value), cameraNodeId ?? Guid.Empty);

    /// <inheritdoc />
    public override Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot)
        => this.Runner.TrySetViewScenePilotAsync(this.context, new(viewId.Value), pilot);

    /// <inheritdoc />
    public override async Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, Guid nodeId)
    {
        var pose = await this.Runner.TryGetViewCameraPoseAsync(this.context, new(viewId.Value), nodeId).ConfigureAwait(false);
        return pose is null ? null : new(pose.Position, pose.RotationDegrees, pose.Scale, pose.OrthographicSize, pose.FieldOfViewDegrees);
    }

    /// <inheritdoc />
    public override async Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId)
        => NativeSessionConversions.FromNative(
            await this.Runner.TryGetViewEditorCameraAsync(this.context, new(viewId.Value)).ConfigureAwait(false));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode)
        => this.Runner.TrySetViewCameraControlModeAsync(this.context, new(viewId.Value), NativeSessionConversions.ToNative<CameraControlModeManaged>(mode));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond)
        => this.Runner.TrySetViewCameraMovementSpeedAsync(this.context, new(viewId.Value), speedUnitsPerSecond);

    /// <inheritdoc />
    public override Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
        => this.Runner.TrySetViewCameraSettingsAsync(this.context, new(viewId.Value), fieldOfViewDegrees, nearPlane, farPlane);

    /// <inheritdoc />
    public override Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options)
        => this.Runner.TrySetViewRenderOptionsAsync(
            this.context,
            new(viewId.Value),
            NativeSessionConversions.ToNative<ViewModeManaged>(options.ViewMode),
            options.ShowGrid,
            options.ShowSelectionOutline);

    /// <inheritdoc />
    public override async Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect)
        => NativeSessionConversions.FromNative(
            await this.Runner.TryPickViewAsync(this.context, new(viewId.Value), rect.X, rect.Y, rect.Width, rect.Height).ConfigureAwait(false));

    /// <inheritdoc />
    public override async Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<Guid> nodeIds)
        => NativeSessionConversions.FromNative(
            await this.Runner.TryFrameViewAsync(this.context, new(viewId.Value), [.. nodeIds]).ConfigureAwait(false));

    /// <inheritdoc />
    public override bool SetSelectionOutline(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId)
        => this.Runner.TrySetSelectionOutline(this.context, [.. nodeIds], activeNodeId);

    /// <inheritdoc />
    public override bool SetTransformGizmo(RuntimeTransformGizmo gizmo)
        => this.Runner.TrySetTransformGizmo(
            this.context,
            NativeSessionConversions.ToNative<TransformToolManaged>(gizmo.Tool),
            NativeSessionConversions.ToNative<TransformSpaceManaged>(gizmo.Space),
            gizmo.Snap.Enabled,
            gizmo.Snap.Translation,
            gizmo.Snap.RotationDegrees,
            gizmo.Snap.Scale,
            [.. gizmo.Targets],
            gizmo.ActiveNodeId,
            gizmo.DisplayScale);

    /// <inheritdoc />
    public override bool CancelTransformGizmoDrag()
        => this.Runner.TryCancelTransformGizmoDrag(this.context);

    /// <inheritdoc />
    public override bool SetTransformGizmoListener(Action<RuntimeGizmoEvent>? listener)
        => this.Runner.TrySetTransformGizmoListener(
            this.context,
            listener is null ? null : native => listener(NativeSessionConversions.FromNative(native)));

    /// <inheritdoc />
    public override RuntimeFrameStatistics GetFrameStatistics()
    {
        var statistics = this.Runner.GetFrameStatistics(this.context);
        return new(statistics.FramesPerSecond, statistics.FrameTimeMilliseconds);
    }
}
