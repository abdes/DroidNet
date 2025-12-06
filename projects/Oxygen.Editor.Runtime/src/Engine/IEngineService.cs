// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Oxygen.Interop.World;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Coordinates the lifetime of the native engine and arbitrates access to composition surfaces across all viewports.
/// </summary>
public interface IEngineService : IAsyncDisposable
{
    /// <summary>
    /// Gets the current lifecycle state of the service.
    /// </summary>
    public EngineServiceState State { get; }

    /// <summary>
    /// Gets the number of active composition surfaces managed by the service.
    /// </summary>
    public int ActiveSurfaceCount { get; }

    /// <summary>
    /// Gets the world instance associated with this engine service.
    /// </summary>
    public OxygenWorld? World { get; }

    /// <summary>
    /// Ensures the underlying engine is initialized in its dormant headless state.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes once the engine is ready for surface allocation.</returns>
    public ValueTask InitializeAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Attaches the engine to a WinUI panel and returns a handle that can be used for subsequent resize and disposal operations.
    /// </summary>
    /// <param name="request">The logical viewport request.</param>
    /// <param name="panel">The swap chain panel that will host the engine output.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that yields the active lease.</returns>
    /// <remarks>
    /// Implementations must enforce <see cref="EngineSurfaceLimits"/> and throw <see cref="InvalidOperationException"/>
    /// when the request would exceed them.
    /// </remarks>
    public ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default);

    /// <summary>
    /// Releases every surface leased by the specified document, allowing the slots to be reused by other files.
    /// </summary>
    /// <param name="documentId">The owning document.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when the releases have finished.</returns>
    public ValueTask ReleaseDocumentSurfacesAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns the current engine target frames-per-second setting.
    /// </summary>
    /// <remarks>
    /// This API is only valid while the <see cref="EngineService"/> is in <see cref="EngineServiceState.Created"/>. When
    /// called in that state the implementation will call into the managed engine wrapper to read the current runtime value.
    /// </remarks>
    public uint GetEngineTargetFps();

    /// <summary>
    /// Updates the engine target frames-per-second runtime setting.
    /// </summary>
    /// <param name="fps">Target frames per second. 0 = uncapped.</param>
    /// <remarks>
    /// This API is only valid while the <see cref="EngineService"/> is in <see cref="EngineServiceState.Created"/> and will
    /// forward the request to the managed native wrapper.
    /// </remarks>
    public void SetEngineTargetFps(uint fps);

    /// <summary>
    /// Maximum allowed target frames per second as defined by the native engine.
    /// </summary>
    public uint MaxTargetFps { get; }

    /// <summary>
    /// Gets the minimum allowed logging verbosity for the native engine runtime
    /// (e.g. loguru::Verbosity_OFF = -9).
    /// </summary>
    public int MinLoggingVerbosity { get; }

    /// <summary>
    /// Gets the maximum allowed logging verbosity for the native engine runtime
    /// (e.g. loguru::Verbosity_MAX = +9).
    /// </summary>
    public int MaxLoggingVerbosity { get; }

    /// <summary>
    /// Gets the current engine native logging verbosity.
    /// </summary>
    /// <returns>Raw verbosity level (may be negative).</returns>
    public int GetEngineLoggingVerbosity();

    /// <summary>
    /// Sets the engine native logging verbosity via the engine's logguru bridge.
    /// This updates the native engine log level and does not change the .NET bridge logger.
    /// </summary>
    /// <param name="verbosity">Logging verbosity, typically between MinLoggingVerbosity and MaxLoggingVerbosity.</param>
    public void SetEngineLoggingVerbosity(int verbosity);

    /// <summary>
    /// Create an Editor view in the native engine using the supplied managed
    /// <see cref="ViewConfigManaged"/>. Returns a Task that completes with the
    /// engine-assigned view id on success or an invalid id on failure.
    /// </summary>
    /// <param name="config">Configuration used to create the view.</param>
    /// <param name="cancellationToken">Optional cancellation token.</param>
    public System.Threading.Tasks.Task<ViewIdManaged> CreateViewAsync(ViewConfigManaged config, System.Threading.CancellationToken cancellationToken = default);

    /// <summary>
    /// Destroy a previously created engine view. Returns true if the destroy
    /// request was accepted by the native engine.
    /// </summary>
    public System.Threading.Tasks.Task<bool> DestroyViewAsync(ViewIdManaged viewId, System.Threading.CancellationToken cancellationToken = default);
}
