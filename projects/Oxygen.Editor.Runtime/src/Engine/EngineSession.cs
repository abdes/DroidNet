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

    /// <summary>Releases context ownership after loop termination.</summary>
    public abstract void DestroyContext();

    /// <summary>Releases runner ownership after loop and callback completion.</summary>
    public abstract void DestroyRunner();
}
