// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The native ownership boundary used by the service lifecycle.</summary>
internal abstract class EngineSession
{
    /// <summary>Gets the native runner for normal runtime operations.</summary>
    public abstract EngineRunner Runner { get; }

    /// <summary>Gets the context still owned by this session.</summary>
    public abstract EngineContext? Context { get; }

    /// <summary>Gets the internal runtime command transport for the owned context.</summary>
    public abstract IRuntimeCommandTransport Commands { get; }

    /// <summary>Gets a value indicating whether runner ownership remains.</summary>
    public abstract bool HasRunner { get; }

    /// <summary>Gets a value indicating whether context ownership remains.</summary>
    public abstract bool HasContext { get; }

    /// <summary>Initializes native ownership; partially created resources remain owned on failure.</summary>
    /// <param name="config">The startup configuration.</param>
    /// <param name="logger">The optional native logger.</param>
    public abstract void Initialize(EditorEngineConfigManaged config, ILogger? logger);

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

    /// <summary>Releases context ownership after loop termination.</summary>
    public abstract void DestroyContext();

    /// <summary>Releases runner ownership after loop and callback completion.</summary>
    public abstract void DestroyRunner();
}
