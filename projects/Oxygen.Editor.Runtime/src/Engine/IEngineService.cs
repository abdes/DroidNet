// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Oxygen.Interop;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Coordinates the lifetime of the native engine, and its component interfaces. Arbitrates
///     access to composition surfaces and views used by the editor.
/// </summary>
public interface IEngineService : IAsyncDisposable
{
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
    ///     logic error, and will cause the immediate termination of the application process in
    ///     debug builds.
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
    ///     logic error, and will cause the immediate termination of the application process in
    ///     debug builds.
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
    ///     logic error, and will cause the immediate termination of the application process in
    ///     debug builds.
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
    ///     logic error, and will cause the immediate termination of the application process in
    ///     debug builds.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    public int ActiveSurfaceCount { get; }

    /// <summary>
    ///     Gets the world instance associated with this engine service. This will be used to mutate
    ///     and query the engine world, including scenes and scene objects. May be null if the
    ///     engine is not yet initialized.
    /// </summary>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state is considered a
    ///     logic error, and will cause the immediate termination of the application process in
    ///     debug builds.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    public OxygenWorld World { get; }

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

    /// <summary>
    ///     Gets the input bridge instance associated with this engine service. This provides
    ///     managed access to runtime input facilities of the native engine.
    ///     May be null if the engine is not yet initialized.
    /// </summary>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Ready"/></item>
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    /// </remarks>
    public OxygenInput Input { get; }

    /// <summary>
    ///     Initializes the runtime engine.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token, that can be used to cancel the operation.</param>
    /// <returns>A <see cref="ValueTask"/> that yields a boolean indicating success or failure.</returns>
    /// <remarks>
    ///     Allowed only in the following states, and using it in any other state will have no effect.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.NoEngine"/></item>
    ///      <item><see cref="EngineServiceState.Faulted"/></item>
    ///     </list>
    ///     Immediately transitions the service to the <c>Initializing</c> state until
    ///     initialization completes, at which point the service will transition to either the
    ///     <c>Ready</c> state on success, or the <c>NoEngine</c> state on failure.
    ///     <para>
    ///     This operation may take time, and can be cancelled via the provided cancellation token.
    ///     When cancelled, the initialization process will be aborted, the runtime engine instance
    ///     will be destroyed, and the service will transition back to the <c>NoEngine</c>
    ///     state.</para>
    /// </remarks>
    /// <seealso cref="EngineServiceState"/>
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.</para>
    ///     <para>
    ///     Immediately transitions the service to the <c>Starting</c> state until completion, at
    ///     which point the service will transition to the <c>Running</c> state upon success, and to
    ///     the <c>Faulted</c> state on failure.</para>
    /// </remarks>
    public ValueTask StartAsync();

    /// <summary>
    ///     Shuts down and disposes the runtime engine.
    /// </summary>
    /// <returns>A <see cref="ValueTask"/> that completes when the engine service has stopped.</returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     <b>Not</b> allowed in the following states, and breaking that is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Initializing"/></item>
    ///      <item><see cref="EngineServiceState.Starting"/></item>
    ///      <item><see cref="EngineServiceState.ShuttingDown"/></item>
    ///     </list>
    ///     Has no effect when called in <c>NoEngine</c> state. When called in any other allowed
    ///     state, and the engine not already shutdown, it immediately transitions the service to
    ///     the <c>ShuttingDown</c> state, the frame loop will be stopped if running, and once the
    ///     shutdown process is complete, the service will transition back to the <c>NoEngine</c>
    ///     state on success, or to the <c>Faulted</c> state on failure.
    ///     <para>
    ///     This operation will take some time, and is not cancellable.</para>
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
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

    /// <summary>
    ///     Releases every surface leased by the specified document, allowing the slots to be reused
    ///     for other surface leases.
    /// </summary>
    /// <param name="documentId">The owning document.</param>
    /// <returns>A <see cref="ValueTask"/> that completes when the releases have finished.</returns>
    /// <throws cref="InvalidOperationException">>If used in an invalid state.</throws>
    /// <remarks>
    ///     Allowed only in the following states:
    ///     <list type="bullet">
    ///      <item><see cref="EngineServiceState.Running"/></item>
    ///     </list>
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
    /// </remarks>
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
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
    ///     Using it in any other state is considered a logic error, and will cause the immediate
    ///     termination of the application process in debug builds.
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
}
