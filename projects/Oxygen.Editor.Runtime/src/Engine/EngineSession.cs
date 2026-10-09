// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The native ownership boundary used by the service lifecycle.</summary>
internal abstract class EngineSession
{
    /// <summary>Gets or sets native logging verbosity.</summary>
    public abstract int LoggingVerbosity { get; set; }

    /// <summary>Gets the native maximum target frame rate.</summary>
    public abstract uint MaxTargetFps { get; }

    /// <summary>Gets or sets the native target frame rate.</summary>
    public abstract uint TargetFps { get; set; }

    /// <summary>Gets the internal runtime command transport for the owned context.</summary>
    public abstract IRuntimeCommandTransport Commands { get; }

    /// <summary>Gets a value indicating whether runner ownership remains.</summary>
    public abstract bool HasRunner { get; }

    /// <summary>Gets a value indicating whether context ownership remains.</summary>
    public abstract bool HasContext { get; }

    /// <summary>Initializes native ownership; partially created resources remain owned on failure.</summary>
    /// <param name="settings">The managed startup settings.</param>
    /// <param name="editorCVarsArchivePath">The editor configuration archive path.</param>
    /// <param name="logger">The optional native logger.</param>
    public abstract void Initialize(IEngineSettings settings, string? editorCVarsArchivePath, ILogger? logger);

    /// <summary>Starts the loop and returns its lifetime task.</summary>
    /// <returns>The operation completion task.</returns>
    public abstract Task RunAsync();

    /// <summary>Waits until native subsystem startup and module registration are complete.</summary>
    /// <returns>The startup acknowledgement, cancellation, or startup failure.</returns>
    public abstract Task WaitForStartupAsync();

    /// <summary>Requests loop termination.</summary>
    public abstract void Stop();

    /// <summary>Turns graphics vsync on or off for every viewport surface.</summary>
    /// <param name="enabled">Whether presents wait for the display's vertical blank.</param>
    public abstract void SetVSyncEnabled(bool enabled);

    /// <summary>Chooses whether every visible viewport pane renders each frame.</summary>
    /// <param name="alwaysRender">Whether idle panes render too.</param>
    public abstract void SetAlwaysRenderPanes(bool alwaysRender);

    /// <summary>Waits for native loop exit cleanup, including dispatcher work.</summary>
    /// <returns>The operation completion task.</returns>
    public abstract Task CompleteLoopCleanupAsync();

    /// <summary>Registers a surface with the running engine.</summary>
    /// <param name="key">The surface identity.</param>
    /// <param name="panel">The composition panel.</param>
    /// <returns>The operation completion task.</returns>
    public abstract Task<bool> RegisterSurfaceAsync(ViewportSurfaceKey key, SwapChainPanel panel);

    /// <summary>Unregisters a surface while the loop can process removal.</summary>
    /// <param name="viewportId">The viewport identity.</param>
    /// <returns>The operation completion task.</returns>
    public abstract Task<bool> UnregisterSurfaceAsync(Guid viewportId);

    /// <summary>Resizes a registered surface.</summary>
    /// <param name="viewportId">The viewport identity.</param>
    /// <param name="width">The width in pixels.</param>
    /// <param name="height">The height in pixels.</param>
    /// <returns>The operation completion task.</returns>
    public abstract Task<bool> ResizeSurfaceAsync(Guid viewportId, uint width, uint height);

    /// <summary>Creates a runtime view.</summary>
    /// <param name="config">The config value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config);

    /// <summary>Destroys a runtime view.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> DestroyViewAsync(RuntimeViewId viewId);

    /// <summary>Shows a runtime view.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> ShowViewAsync(RuntimeViewId viewId);

    /// <summary>Hides a runtime view.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> HideViewAsync(RuntimeViewId viewId);

    /// <summary>Sets the editor camera preset.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="preset">The preset value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset);

    /// <summary>Renders a view through an authored scene camera, or through its editor camera.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="cameraNodeId">The camera node id, or <see langword="null"/> for the editor camera.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, Guid? cameraNodeId);

    /// <summary>Starts or stops piloting the scene camera a view looks through.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="pilot">Whether navigation moves the scene camera.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot);

    /// <summary>Reads the pose that would place a node at a view's editor camera.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="nodeId">The nodeId value.</param>
    /// <returns>The pose, or <see langword="null"/> when the view or node does not exist.</returns>
    public abstract Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, Guid nodeId);

    /// <summary>Reads a view's editor camera state.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <returns>The state, or <see langword="null"/> when the view does not exist.</returns>
    public abstract Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId);

    /// <summary>Sets the editor camera navigation mode.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="mode">The mode value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode);

    /// <summary>Sets the editor camera movement speed.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="speedUnitsPerSecond">The speedUnitsPerSecond value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond);

    /// <summary>Sets the editor camera lens and clipping planes.</summary>
    /// <param name="viewId">The viewId value.</param>
    /// <param name="fieldOfViewDegrees">The fieldOfViewDegrees value.</param>
    /// <param name="nearPlane">The nearPlane value.</param>
    /// <param name="farPlane">The farPlane value.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane);

    /// <summary>Sets how a view presents the scene.</summary>
    /// <param name="viewId">The view to update.</param>
    /// <param name="options">How the view presents the scene.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options);

    /// <summary>Picks the scene nodes with visible geometry inside a rectangle of a view.</summary>
    /// <param name="viewId">The view to pick.</param>
    /// <param name="rect">The rectangle, in physical pixels of the view's surface.</param>
    /// <returns>The hits, or <see langword="null"/> when no current result exists.</returns>
    public abstract Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect);

    /// <summary>Frames scene nodes, or the whole scene, in a view.</summary>
    /// <param name="viewId">The view whose editor camera moves.</param>
    /// <param name="nodeIds">The nodes to frame; empty frames the whole scene.</param>
    /// <returns>What the request did.</returns>
    public abstract Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<Guid> nodeIds);

    /// <summary>Outlines the selected nodes in every editing view.</summary>
    /// <param name="nodeIds">The selected nodes; empty clears the outline.</param>
    /// <param name="activeNodeId">The active node, if any.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract bool SetSelectionOutline(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId);

    /// <summary>Shows the transform gizmo for the given targets in every editing view.</summary>
    /// <param name="gizmo">The tool, space, snapping and targets.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract bool SetTransformGizmo(RuntimeTransformGizmo gizmo);

    /// <summary>Cancels a running gizmo or helper handle drag at the next frame.</summary>
    /// <returns>The runtime operation result.</returns>
    public abstract bool CancelTransformGizmoDrag();

    /// <summary>Sets the workspace state the light and camera helpers follow.</summary>
    /// <param name="helpers">The hidden and locked nodes and the display scale.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract bool SetSceneHelpers(RuntimeSceneHelpers helpers);

    /// <summary>Receives the gizmo's and the scene helpers' interactions on the engine thread.</summary>
    /// <param name="listener">The receiver, or <see langword="null"/> to stop.</param>
    /// <returns>The runtime operation result.</returns>
    public abstract bool SetTransformGizmoListener(Action<RuntimeGizmoEvent>? listener);

    /// <summary>Gets the rate and duration of the last completed engine frame.</summary>
    /// <returns>The frame statistics.</returns>
    public abstract RuntimeFrameStatistics GetFrameStatistics();

    /// <summary>Releases context ownership after loop termination.</summary>
    public abstract void DestroyContext();

    /// <summary>Releases runner ownership after loop and callback completion.</summary>
    public abstract void DestroyRunner();
}
