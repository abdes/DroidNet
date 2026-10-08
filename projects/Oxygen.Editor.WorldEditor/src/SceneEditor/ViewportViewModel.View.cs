// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// The pane's engine view lifetime. The pane keeps its camera state while it has no view (dock
/// move, document switch, maximize) and recreates the view with it, so the view is correct on its
/// first presented frame. It also owns the camera preview inset composed over its view.
/// </summary>
public partial class ViewportViewModel
{
    private const string ViewPurpose = "Viewport";
    private const string InsetPurpose = "CameraPreview";

    // View creation, release and inset changes run one at a time, in request order.
    private Task viewWork = Task.CompletedTask;
    private Guid? surfaceId;
    private string viewName = "viewport";
    private RuntimeViewId insetViewId = RuntimeViewId.Invalid;
    private SceneCameraChoice? insetCamera;
    private SceneCameraChoice? requestedInsetCamera;

    /// <summary>
    /// Gets the editor camera state this pane last read from its view, which a recreated view starts
    /// from; <see langword="null"/> until the pane has had a view, so a new view frames the scene.
    /// </summary>
    public RuntimeEditorCamera? EditorCamera { get; private set; }

    /// <summary>Gets the scene camera the preview inset currently shows, if any.</summary>
    public SceneCameraChoice? InsetCamera => this.insetCamera;

    /// <summary>
    /// Creates this pane's engine view on a surface, starting from the pane's camera state: preset,
    /// editor camera, viewed scene camera, control mode, lens settings and pilot.
    /// </summary>
    /// <param name="surface">The surface the view presents to.</param>
    /// <param name="name">The diagnostic view name.</param>
    /// <param name="width">The initial width in pixels.</param>
    /// <param name="height">The initial height in pixels.</param>
    /// <returns>A task that completes when the view exists or its creation failed.</returns>
    internal Task CreateViewAsync(Guid surface, string name, uint width, uint height)
        => this.RunViewWorkAsync(() => this.CreateViewCoreAsync(surface, name, width, height));

    /// <summary>
    /// Releases this pane's engine view and inset, first reading the editor camera state the pane
    /// keeps for its next view.
    /// </summary>
    /// <returns>A task that completes when the view was released.</returns>
    internal Task ReleaseViewAsync() => this.RunViewWorkAsync(this.ReleaseViewCoreAsync);

    /// <summary>
    /// Shows a camera preview inset for <paramref name="camera"/>, or hides it. The inset stays
    /// hidden while the pane looks through that camera itself.
    /// </summary>
    /// <param name="camera">The camera to preview, or <see langword="null"/> to hide the inset.</param>
    /// <returns>A task that completes when the inset matches the request.</returns>
    internal Task SetInsetCameraAsync(SceneCameraChoice? camera)
    {
        this.requestedInsetCamera = camera;
        return this.RunViewWorkAsync(this.ReconcileInsetCoreAsync);
    }

    /// <summary>Reads the editor camera state from the pane's view, keeping the last one when it cannot.</summary>
    /// <returns>A task that completes when <see cref="EditorCamera"/> is current.</returns>
    internal Task RefreshEditorCameraAsync() => this.RunViewWorkAsync(this.RefreshEditorCameraCoreAsync);

    /// <summary>
    /// Restores the pane's kept state before it has a view. A pane never reopens piloting: the
    /// camera it piloted is only looked through.
    /// </summary>
    /// <param name="state">The kept pane state.</param>
    /// <param name="sceneCamera">The scene camera to look through, resolved from the kept state, if any.</param>
    internal void RestoreState(ViewportPaneState state, SceneCameraChoice? sceneCamera)
    {
        this.CameraType = state.CameraType;
        this.CameraControlMode = state.ControlMode;
        this.EditorCamera = state.EditorCamera?.ToRuntime();
        this.SceneCamera = sceneCamera;
        this.IsPilotingSceneCamera = false;
        this.ViewMode = state.ViewMode;
        this.ShowGrid = state.ShowGrid;
        this.ShowSelectionOutline = state.ShowSelectionOutline;
        this.ShowCameraPreview = state.ShowCameraPreview;
        this.ShowStatistics = state.ShowStatistics;
    }

    /// <summary>Captures the state the pane keeps across sessions.</summary>
    /// <returns>The pane state, with the editor camera last read from the view.</returns>
    internal ViewportPaneState CaptureState()
        => new(
            this.CameraType,
            this.CameraControlMode,
            ViewportCameraState.From(this.EditorCamera),
            this.SceneCamera?.NodeId,
            this.ViewMode,
            this.ShowGrid,
            this.ShowCameraPreview,
            this.ShowStatistics,
            this.ShowSelectionOutline);

    private static CameraViewPreset ToPreset(CameraType type) => type switch
    {
        CameraType.Top => CameraViewPreset.Top,
        CameraType.Bottom => CameraViewPreset.Bottom,
        CameraType.Left => CameraViewPreset.Left,
        CameraType.Right => CameraViewPreset.Right,
        CameraType.Front => CameraViewPreset.Front,
        CameraType.Back => CameraViewPreset.Back,
        _ => CameraViewPreset.Perspective,
    };

    private static async Task RunAfterAsync(Task previous, Func<Task> work)
    {
        // Every step reports its own failures; an earlier one never blocks the next.
        _ = await Task.WhenAny(previous).ConfigureAwait(true);
        await work().ConfigureAwait(true);
    }

    private Task RunViewWorkAsync(Func<Task> work)
    {
        var run = RunAfterAsync(this.viewWork, work);
        this.viewWork = run;
        return run;
    }

    private async Task CreateViewCoreAsync(Guid surface, string name, uint width, uint height)
    {
        if (this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var created = await this.EngineService.CreateViewAsync(new RuntimeViewConfig
            {
                Name = name,
                Purpose = ViewPurpose,
                CompositingTarget = surface,
                Width = width,
                Height = height,
                ClearColor = this.ClearColor,
                CameraPreset = ToPreset(this.CameraType),
                EditorCamera = this.EditorCamera,
                SceneCameraNodeId = this.SceneCamera?.NodeId,
                ViewMode = this.ViewMode,
                ShowGrid = this.ShowGrid,
                ShowSelectionOutline = this.ShowSelectionOutline,
            }).ConfigureAwait(true);
            if (!created.IsValid)
            {
                this.PublishRuntimeFailure(
                    RuntimeOperationKinds.ViewCreate,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CREATE_REJECTED",
                    "Viewport view was not created",
                    "The runtime rejected the engine view creation request for this viewport.");
                return;
            }

            this.AssignedViewId = created;
            this.AssignedInputTarget = this.EngineService.InputCommands.GetViewTarget(created.Value);
            this.surfaceId = surface;
            this.viewName = name;
            this.LogViewCreated(created);

            // Preset, cameras, view mode and grid were part of the creation; these follow it.
            await this.ApplyCurrentCameraControlModeAsync().ConfigureAwait(true);
            await this.ApplyCurrentCameraSettingsAsync().ConfigureAwait(true);
            if (this.IsPilotingSceneCamera)
            {
                _ = await this.SendScenePilotAsync(pilot: true).ConfigureAwait(true);
            }

            await this.ReconcileInsetCoreAsync().ConfigureAwait(true);
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogCreateViewFailed(ex);
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewCreate,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CREATE_FAILED",
                "Viewport view creation failed",
                "The runtime could not create an engine view for this viewport.",
                ex);
        }
    }

    private async Task ReleaseViewCoreAsync()
    {
        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        await this.DestroyInsetCoreAsync().ConfigureAwait(true);
        await this.RefreshEditorCameraCoreAsync().ConfigureAwait(true);

        var viewId = this.AssignedViewId;
        this.AssignedViewId = RuntimeViewId.Invalid;
        this.AssignedInputTarget = null;
        this.surfaceId = null;
        try
        {
            if (!await this.EngineService.DestroyViewAsync(viewId).ConfigureAwait(true))
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewDestroy,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "DESTROY_REJECTED",
                    "Viewport view teardown was rejected",
                    "The runtime rejected the engine view teardown request for this viewport.");
                return;
            }

            this.LogViewDestroyed();
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogDestroyViewFailed(ex);
            this.PublishRuntimeWarning(
                RuntimeOperationKinds.ViewDestroy,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "DESTROY_FAILED",
                "Viewport view teardown failed",
                "The runtime could not destroy the engine view for this viewport.",
                ex);
        }
    }

    private async Task RefreshEditorCameraCoreAsync()
    {
        if (!this.AssignedViewId.IsValid || this.EngineService.State != EngineServiceState.Running)
        {
            return;
        }

        try
        {
            if (await this.EngineService.GetViewEditorCameraAsync(this.AssignedViewId).ConfigureAwait(true) is { } camera)
            {
                this.EditorCamera = camera;
                return;
            }

            this.LogEditorCameraUnavailable();
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            // The engine stopping under the pane loses only the camera moves since the last read.
            this.LogEditorCameraUnavailable(ex);
        }
    }

    private async Task ReconcileInsetCoreAsync()
    {
        var desired = this.AssignedViewId.IsValid && this.surfaceId is not null && this.ShowCameraPreview
            && this.requestedInsetCamera is { } requested && this.SceneCamera?.NodeId != requested.NodeId
            ? requested
            : null;
        if (desired is null)
        {
            await this.DestroyInsetCoreAsync().ConfigureAwait(true);
            return;
        }

        if (this.insetViewId.IsValid && this.insetCamera?.NodeId == desired.NodeId)
        {
            this.insetCamera = desired;
            return;
        }

        try
        {
            if (this.insetViewId.IsValid)
            {
                if (await this.EngineService.SetViewSceneCameraAsync(this.insetViewId, desired.NodeId).ConfigureAwait(true))
                {
                    this.insetCamera = desired;
                }

                return;
            }

            var created = await this.EngineService.CreateViewAsync(new RuntimeViewConfig
            {
                Name = this.viewName + ":" + InsetPurpose,
                Purpose = InsetPurpose,
                CompositingTarget = this.surfaceId,
                ClearColor = this.ClearColor,
                SceneCameraNodeId = desired.NodeId,
                InsetHost = this.AssignedViewId,
            }).ConfigureAwait(true);
            if (created.IsValid)
            {
                this.insetViewId = created;
                this.insetCamera = desired;
            }
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogInsetFailed(ex);
        }
    }

    private async Task DestroyInsetCoreAsync()
    {
        if (!this.insetViewId.IsValid)
        {
            return;
        }

        var inset = this.insetViewId;
        this.insetViewId = RuntimeViewId.Invalid;
        this.insetCamera = null;
        try
        {
            _ = await this.EngineService.DestroyViewAsync(inset).ConfigureAwait(true);
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogInsetFailed(ex);
        }
    }
}
