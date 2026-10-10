// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Coordinates the lifetime of the native engine, and its component interfaces. Arbitrates
///     access to composition surfaces and views used by the editor.
/// </summary>
public interface IEngineService : IAsyncDisposable
{
    /// <summary>Reports lifecycle transitions and unexpected loop termination.</summary>
    /// <remarks>
    /// Notifications are ordered and delivered outside the lifecycle gate, on the thread that
    /// completes the operation. UI consumers must dispatch to their UI thread and use the run
    /// identity to distinguish notifications from an earlier lifetime.
    /// </remarks>
    public event EventHandler<EngineStateChangedEventArgs>? StateChanged;

    /// <summary>Reports ordered content transitions asynchronously; UI consumers must dispatch to their UI thread.</summary>
    public event EventHandler<RuntimeContentChangedEventArgs>? ContentStatusChanged;

    /// <summary>
    /// Reports the transform gizmo's hover and drag interactions in order, asynchronously; UI
    /// consumers must dispatch to their UI thread. The gizmo never changes the scene: the editor
    /// applies a drag's results through its authoring commands.
    /// </summary>
    public event EventHandler<RuntimeGizmoEventArgs>? GizmoEvent;

    /// <summary>
    ///     Gets the current lifecycle state of the service.
    /// </summary>
    public EngineServiceState State { get; }

    /// <summary>Gets acknowledged native content availability, separate from publication and freshness.</summary>
    public RuntimeContentSnapshot ContentStatus { get; }

    /// <summary>
    ///     Gets or sets the current native engine logging verbosity. This operates only on the
    ///     native logging system via the engine's logguru bridge and does not affect the .NET
    ///     bridge ILogger.
    /// </summary>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <throws cref="ArgumentOutOfRangeException">
    ///     If the provided value is less than <see cref="EngineConstants.MinLoggingVerbosity"/> or
    ///     greater than <see cref="EngineConstants.MaxLoggingVerbosity"/>.
    /// </throws>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state is considered a
    ///     logic error and throws an exception.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    /// <seealso cref="EngineConstants.MinLoggingVerbosity"/>
    /// <seealso cref="EngineConstants.MaxLoggingVerbosity"/>
    public int EngineLoggingVerbosity { get; set; }

    /// <summary>
    ///     Gets the maximum target frames per second supported by the native engine.
    /// </summary>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state is considered a
    ///     logic error and throws an exception.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    public uint MaxTargetFps { get; }

    /// <summary>
    ///     Gets or sets the current engine target frames-per-second setting. The value will always
    ///     be clamped between 0 and <see cref="MaxTargetFps"/>, with <c>0</c> meaning unlimited.
    /// </summary>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state is considered a
    ///     logic error and throws an exception.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    /// <seealso cref="MaxTargetFps"/>
    public uint TargetFps { get; set; }

    /// <summary>
    ///     Gets the number of active composition surfaces managed by the service. This count is
    ///     always less than or equal to <see cref="EngineConstants.MaxTotalSurfaces"/>.
    /// </summary>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state is considered a
    ///     logic error and throws an exception.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    public int ActiveSurfaceCount { get; }

    /// <summary>Gets managed scene commands, including an explicit unavailable outcome before startup.</summary>
    public IRuntimeWorldCommands WorldCommands { get; }

    /// <summary>Gets managed viewport input commands with run and view-generation validation.</summary>
    public IRuntimeInputCommands InputCommands { get; }

    /// <summary>Refreshes all project cooked roots and the current scene's asset bindings.</summary>
    /// <param name="bindings">The complete validated root order and owned logical mounts.</param>
    /// <param name="readLease">Optional read ownership transferred to the native session, including on failure.</param>
    /// <param name="keepPaused">Whether the publisher will resume rendering after committing metadata.</param>
    /// <returns>Completion after native loading and current binding replacement settle.</returns>
    public Task RefreshProjectCookedRootsAsync(IReadOnlyList<RuntimeCookedRoot> bindings, IDisposable? readLease = null, bool keepPaused = false);

    /// <summary>Turns graphics vsync on or off for every viewport; the engine applies it at its next frame.</summary>
    /// <param name="enabled">Whether presents wait for the display's vertical blank.</param>
    /// <throws cref="InvalidOperationException">If the service is neither ready nor running.</throws>
    public void SetVSyncEnabled(bool enabled);

    /// <summary>Chooses whether every visible viewport pane renders each frame.</summary>
    /// <param name="alwaysRender">
    ///     <see langword="true"/> to render every visible pane each frame; <see langword="false"/> to render a pane
    ///     only when what it shows may have changed.
    /// </param>
    /// <throws cref="InvalidOperationException">If the service is neither ready nor running.</throws>
    public void SetAlwaysRenderPanes(bool alwaysRender);

    /// <summary>Sets how every viewport draws the ground grid; a viewport's own grid toggle still hides it there.</summary>
    /// <param name="settings">The grid settings, within the ranges the engine accepts.</param>
    /// <throws cref="InvalidOperationException">If the service is neither ready nor running.</throws>
    public void SetGroundGrid(GroundGridSettings settings);

    /// <summary>Suspends preview and drains native I/O while retaining accepted immutable roots and their readers.</summary>
    /// <returns>The native suspension acknowledgement.</returns>
    public Task SuspendCookedContentAsync();

    /// <summary>Resumes the current authoring preview after publication or restoration.</summary>
    /// <returns>The native resume acknowledgement.</returns>
    public Task ResumeCookedContentAsync();

    /// <summary>Initializes the runtime, first completing cleanup of any previous failed instance.</summary>
    /// <param name="cancellationToken">Cancels waiting for another lifecycle operation; native creation is synchronous.</param>
    /// <returns>A task yielding <see langword="true"/> when ready or already running.</returns>
    /// <remarks>
    /// Lifecycle operations are serialized. Failure preserves the original exception and attempts
    /// partial cleanup. Unreleased ownership remains in <c>Faulted</c> and must be cleaned before
    /// another instance can be initialized. New initialization is rejected after disposal is requested.
    /// </remarks>
    public ValueTask<bool> InitializeAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Starts the runtime engine frame loop.
    /// </summary>
    /// <returns>A <see cref="ValueTask"/> that completes when the engine service has started.</returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Starting"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Will have no effect in <c>Starting</c> or <c>Running</c> states.
    ///     <para>
    ///     Using it in any other state throws an exception.</para>
    ///     <para>
    ///     Immediately transitions the service to the <c>Starting</c> state until completion, at
    ///     which point the service will transition to the <c>Running</c> state upon success, and to
    ///     the <c>Faulted</c> state if the loop fails. Synchronous startup failure attempts cleanup
    ///     before returning the original error.</para>
    /// </remarks>
    public ValueTask StartAsync();

    /// <summary>Shuts down the native engine and reports failures after attempting all safe cleanup.</summary>
    /// <returns>A task that completes after teardown has been attempted.</returns>
    /// <exception cref="AggregateException">One or more cleanup operations failed.</exception>
    /// <remarks>
    /// Lifecycle and asynchronous surface/view operations are serialized. Repeated calls wait
    /// for prior work and may retry incomplete cleanup. This operation is not cancellable.
    /// The state is <see cref="EngineServiceState.NoEngine"/> only when native ownership is
    /// fully released, or <see cref="EngineServiceState.Faulted"/> when resources remain.
    /// Reported failures can include intermediate errors even when final cleanup succeeded.
    /// <see cref="IAsyncDisposable.DisposeAsync"/> uses the same cleanup as a non-throwing,
    /// logged fallback and permanently rejects new runtime work once disposal is requested.
    /// </remarks>
    public ValueTask ShutdownAsync();

    // -- Surface Management --

    /// <summary>
    ///     Attaches the engine to a WinUI swapchain panel and returns a handle that can be used for
    ///     subsequent resize and disposal operations.
    /// </summary>
    /// <param name="request">The logical viewport request.</param>
    /// <param name="panel">The swapchain panel that will host the engine output.</param>
    /// <param name="cancellationToken">A <see cref="CancellationToken"/> that can be used to cancel the attach operation.</param>
    /// <returns>A <see cref="ValueTask"/> that yields the active lease upon completion.</returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <throws cref="InvalidOperationException">
    ///     If the surface limit specified by <see cref="EngineConstants.MaxTotalSurfaces"/> has
    ///     been reached.
    /// </throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state throws an exception.
    ///     <para>
    ///     The provided <paramref name="cancellationToken"/> is observed before the native
    ///     registration call and immediately after a successful registration. If cancellation
    ///     is requested after registration has completed, the implementation will attempt a
    ///     best-effort unregistration of the native surface and then throw an
    ///     <see cref="OperationCanceledException"/>. No absolute guarantee is provided that
    ///     native resources are released immediately.
    ///     </para>
    /// </remarks>
    public ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default);

    /// <summary>Releases every surface leased by the specified document.</summary>
    /// <param name="documentId">The owning document.</param>
    /// <returns>A task that completes after every matching release has been attempted.</returns>
    /// <exception cref="AggregateException">One or more surface releases failed.</exception>
    /// <remarks>Safe during teardown and after shutdown. Failed native releases remain tracked until cleaned up.</remarks>
    public ValueTask ReleaseDocumentSurfacesAsync(Guid documentId);

    // -- View Management --

    /// <summary>
    ///     Create an Editor view in the native engine using the supplied <see cref="RuntimeViewConfig"/>.
    /// </summary>
    /// <param name="config">Configuration used to create the view.</param>
    /// <returns>
    ///     A <see cref="Task{RuntimeViewId}"/> that completes with the engine-assigned view id on
    ///     success, or <see cref="RuntimeViewId.Invalid"/> on failure.
    /// </returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state throws an exception.
    /// </remarks>
    public Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config);

    /// <summary>
    /// Destroy a previously created engine view. Returns true if the destroy
    /// request was accepted by the native engine, or when the engine is no longer
    /// running: a stopped engine has already released every view.
    /// </summary>
    /// <param name="viewId">The id of the view to destroy.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    /// <remarks>
    ///     Safe in every state, including during and after shutdown, so view owners can release
    ///     their views in any teardown order.
    /// </remarks>
    public Task<bool> DestroyViewAsync(RuntimeViewId viewId);

    /// <summary>
    /// Make an existing view visible (resume rendering). Returns true if the
    /// request was accepted by the native engine.
    /// </summary>
    /// <param name="viewId">The id of the view to show.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state throws an exception.
    /// </remarks>
    public Task<bool> ShowViewAsync(RuntimeViewId viewId);

    /// <summary>
    /// Hide an existing view (pause rendering while retaining resources).
    /// Returns true if the request was accepted by the native engine.
    /// </summary>
    /// <param name="viewId">The id of the view to hide.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state throws an exception.
    /// </remarks>
    public Task<bool> HideViewAsync(RuntimeViewId viewId);

    /// <summary>
    /// Set the camera view preset for an existing view (Perspective/Top/etc).
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="preset">The preset to apply.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset);

    /// <summary>
    /// Set the editor camera navigation mode for an existing view.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="mode">The editor camera control mode to apply.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode);

    /// <summary>
    /// Render an existing view through the authored camera on a scene node, or through the
    /// view's editor camera.
    /// </summary>
    /// <remarks>
    /// The runtime resolves the node every frame. While the node is missing or has no camera,
    /// the view renders through its editor camera. The authored camera is never modified, and
    /// editor navigation does not apply while a scene camera is in use.
    /// </remarks>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="cameraNodeId">
    ///     The scene node that carries the camera, or <see langword="null"/> for the editor camera.
    /// </param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, Guid? cameraNodeId);

    /// <summary>
    /// Start or stop piloting the scene camera a view looks through.
    /// </summary>
    /// <remarks>
    /// While piloting, the view's editor camera takes the scene camera's pose and navigation moves
    /// both; the runtime scene camera follows every frame. The authored document is not edited:
    /// callers commit the resulting pose with <see cref="GetViewCameraPoseAsync"/>.
    /// </remarks>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="pilot">Whether navigation should move the scene camera.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot);

    /// <summary>
    /// Read the pose that would place a scene node at a view's editor camera.
    /// </summary>
    /// <param name="viewId">The view whose editor camera supplies the pose.</param>
    /// <param name="nodeId">The node the pose is expressed for, in its parent's space.</param>
    /// <returns>The pose, or <see langword="null"/> when the view or node does not exist.</returns>
    public Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, Guid nodeId);

    /// <summary>
    /// Read a view's editor camera state, so a pane can keep it when its view is released and
    /// recreate the view with it.
    /// </summary>
    /// <param name="viewId">The view whose editor camera is read.</param>
    /// <returns>The state, or <see langword="null"/> when the view does not exist.</returns>
    public Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId);

    /// <summary>
    /// Set the editor camera fly movement speed for an existing view.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="speedUnitsPerSecond">The base movement speed in world units per second.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond);

    /// <summary>
    /// Set the editor camera lens and clipping settings for an existing view.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="fieldOfViewDegrees">The vertical field of view in degrees.</param>
    /// <param name="nearPlane">The near view plane in meters.</param>
    /// <param name="farPlane">The far view plane in meters.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane);

    /// <summary>
    /// Set how an existing view presents the scene: its view mode, ground grid and selection
    /// outline. This is viewport state; it never changes the authored scene.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="options">How the view presents the scene.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options);

    /// <summary>
    /// Pick the scene nodes with visible geometry inside a rectangle of a view. The engine renders
    /// the rectangle on the view's next frame and reads it back a few frames later; an idle view
    /// pays nothing.
    /// </summary>
    /// <param name="viewId">The view to pick.</param>
    /// <param name="rect">The rectangle, in physical pixels of the view's surface.</param>
    /// <returns>
    ///     The hits, or <see langword="null"/> when the view is not rendering or the scene was
    ///     replaced before the result arrived; a stale result never reaches the caller.
    /// </returns>
    public Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect);

    /// <summary>
    /// Frame scene nodes in a view with a short eased move of its editor camera, keeping the view
    /// direction and a 10% margin. Orthographic views resize instead of moving closer. Never
    /// changes the authored scene.
    /// </summary>
    /// <param name="viewId">The view whose editor camera moves.</param>
    /// <param name="nodeIds">The nodes to frame, with their descendants; empty frames the whole scene.</param>
    /// <returns>What the request did.</returns>
    public Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<Guid> nodeIds);

    /// <summary>
    /// Outline the selected nodes, and their descendants, in every editing view that shows the
    /// selection outline. The active node is drawn brighter.
    /// </summary>
    /// <param name="nodeIds">The selected nodes; empty clears the outline.</param>
    /// <param name="activeNodeId">The active (last selected) node, if any.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetSelectionOutlineAsync(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId);

    /// <summary>
    /// Show the transform gizmo of the given targets in every editing view, or hide it for the
    /// Select tool or no targets. A running drag of other targets or with another tool is
    /// cancelled. A press on a gizmo handle is taken from the view's input, so it neither
    /// navigates nor picks.
    /// </summary>
    /// <param name="gizmo">The tool, space, snapping and targets.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetTransformGizmoAsync(RuntimeTransformGizmo gizmo);

    /// <summary>
    /// Cancel a running gizmo drag at the next frame; <see cref="GizmoEvent"/> reports the
    /// cancellation.
    /// </summary>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> CancelTransformGizmoDragAsync();

    /// <summary>
    /// Set the workspace state the editing views' light and camera icons and selected helpers
    /// follow. Their handle drags and orientation triad clicks report through
    /// <see cref="GizmoEvent"/>.
    /// </summary>
    /// <param name="helpers">The hidden and locked nodes and the display scale.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetSceneHelpersAsync(RuntimeSceneHelpers helpers);

    /// <summary>
    /// Gets the rate and duration of the last completed engine frame. Cheap enough to poll from the
    /// UI thread.
    /// </summary>
    /// <returns>The statistics, or <see langword="null"/> when the engine is not running.</returns>
    public RuntimeFrameStatistics? GetFrameStatistics();
}
