// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Oxygen.Interop;

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

    /// <summary>
    ///     Gets the current lifecycle state of the service.
    /// </summary>
    public EngineServiceState State { get; }

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

    /// <summary>
    ///     Mounts the project's cooked assets root directory in the engine's virtual path resolver.
    ///     This allows the engine to resolve virtual paths to actual files on disk within the
    ///     project's cooked assets folder.
    /// </summary>
    /// <param name="path">The absolute path to the project's cooked assets directory.</param>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    public void MountProjectCookedRoot(string path);

    /// <summary>
    ///     Unmounts the project's cooked assets root in the engine's virtual path resolver.
    /// </summary>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    public void UnmountProjectCookedRoot();

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
    ///     Create an Editor view in the native engine using the supplied <see cref="ViewConfigManaged"/>.
    /// </summary>
    /// <param name="config">Configuration used to create the view.</param>
    /// <returns>
    ///     A <see cref="Task{ViewIdManaged}"/> that completes with the engine-assigned view id on
    ///     success, or <see cref="ViewIdManaged.Invalid"/> on failure.
    /// </returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state throws an exception.
    /// </remarks>
    public Task<ViewIdManaged> CreateViewAsync(ViewConfigManaged config);

    /// <summary>
    /// Destroy a previously created engine view. Returns true if the destroy
    /// request was accepted by the native engine.
    /// </summary>
    /// <param name="viewId">The id of the view to destroy.</param>
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
    public Task<bool> DestroyViewAsync(ViewIdManaged viewId);

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
    public Task<bool> ShowViewAsync(ViewIdManaged viewId);

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
    public Task<bool> HideViewAsync(ViewIdManaged viewId);

    /// <summary>
    /// Set the camera view preset for an existing view (Perspective/Top/etc).
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="preset">The preset to apply.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraPresetAsync(ViewIdManaged viewId, CameraViewPresetManaged preset);

    /// <summary>
    /// Set the editor camera navigation mode for an existing view.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="mode">The editor camera control mode to apply.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraControlModeAsync(ViewIdManaged viewId, CameraControlModeManaged mode);

    /// <summary>
    /// Set the editor camera fly movement speed for an existing view.
    /// </summary>
    /// <param name="viewId">The id of the view to update.</param>
    /// <param name="speedUnitsPerSecond">The base movement speed in world units per second.</param>
    /// <returns>
    ///     A <see cref="Task"/> that completes with <see langword="true"/> on success, or
    ///     <see langword="false"/> on failure.
    /// </returns>
    public Task<bool> SetViewCameraMovementSpeedAsync(ViewIdManaged viewId, float speedUnitsPerSecond);

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
    public Task<bool> SetViewCameraSettingsAsync(ViewIdManaged viewId, float fieldOfViewDegrees, float nearPlane, float farPlane);
}
