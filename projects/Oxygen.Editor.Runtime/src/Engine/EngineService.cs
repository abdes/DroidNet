// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Interop;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

// TODO: engine config options for InitializeAsync to be exposed via EngineService
// TODO: auto-tune the engine target FPS based on whether we have active views to render or not

/// <summary>
///     Application-wide coordinator that keeps the native engine alive and arbitrates access to
///     presentation surfaces and views used by the editor.
/// </summary>
/// <param name="hostingContext">Provides access to the UI dispatcher context.</param>
/// <param name="loggerFactory">Optional factory used to bridge native engine logging.</param>
public sealed partial class EngineService(HostingContext hostingContext, ILoggerFactory? loggerFactory = null) : IEngineService
{
    private readonly HostingContext hostingContext = hostingContext;
    private readonly ILogger<EngineService> logger = loggerFactory?.CreateLogger<EngineService>() ?? NullLoggerFactory.Instance.CreateLogger<EngineService>();
    private readonly SemaphoreSlim initializationGate = new(1, 1);
    private readonly SemaphoreSlim leaseGate = new(1, 1);
    private readonly System.Collections.Concurrent.ConcurrentDictionary<Guid, int> documentSurfaceCounts = new();
    private readonly System.Collections.Concurrent.ConcurrentDictionary<ViewportSurfaceKey, ViewportSurfaceLease> activeLeases = new();
    private int reservedSurfaceCount;

#pragma warning disable CA2213 // Disposable fields should be disposed
    private EngineRunner? engineRunner; // disposed in ShutDownAsync, called by DiosposeAsync
    private EngineContext? engineContext; // disposed in TryDestroyEngineContext, called by ShutDownAsync
#pragma warning restore CA2213 // Disposable fields should be disposed
    private Task? engineLoopTask;
    private EngineServiceState state = EngineServiceState.NoEngine;
    private bool disposed;

    /// <inheritdoc/>
    public EngineServiceState State => this.state;

    /// <inheritdoc/>
    public int EngineLoggingVerbosity
    {
        get
        {
            var runner = this.EnsureIsReadyOrRunning();
            var cfg = runner.GetLoggingConfig(this.engineContext);
            return cfg.Verbosity;
        }

        set
        {
            if (value is < EngineConstants.MinLoggingVerbosity or > EngineConstants.MaxLoggingVerbosity)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(value),
                    value,
                    string.Create(CultureInfo.InvariantCulture, $"Logging verbosity must be between {EngineConstants.MinLoggingVerbosity} and {EngineConstants.MaxLoggingVerbosity}."));
            }

            var runner = this.EnsureIsReadyOrRunning();
            try
            {
                var cfg = runner.GetLoggingConfig(this.engineContext);
                cfg.Verbosity = value;
                if (!runner.ConfigureLogging(cfg))
                {
                    this.LogSetLoggingVerbosityFailed(value);
                    throw new InvalidOperationException(string.Create(CultureInfo.InvariantCulture, $"Failed to configure native engine logging with verbosity {value}."));
                }

                this.LogLoggingVerbositySet(value);
            }
            catch (Exception ex)
            {
                this.LogSetLoggingVerbosityFailed(value, ex);
                throw;
            }
        }
    }

    /// <inheritdoc/>
    public uint MaxTargetFps
    {
        get
        {
            _ = this.EnsureIsReadyOrRunning();
            return EngineConfig.MaxTargetFps; // FIXME: this should be queried from the engine as it will change based on monitor, and other factors
        }
    }

    /// <inheritdoc/>
    public uint TargetFps
    {
        get
        {
            var runner = this.EnsureIsReadyOrRunning();
            var cfg = runner.GetEngineConfig(this.engineContext);
            Debug.Assert(cfg is not null, "A ready or running engine should return a valid EngineConfig object");
            return cfg.TargetFps;
        }

        set
        {
            var runner = this.EnsureIsReadyOrRunning();
            var clamped = Math.Clamp(value, 0, this.MaxTargetFps);
            runner.SetTargetFps(this.engineContext, value);
            this.LogTargetFpsSet(value, clamped);
        }
    }

    /// <inheritdoc/>
    public int ActiveSurfaceCount
    {
        get
        {
            _ = this.EnsureIsReadyOrRunning();

            // Use the concurrent dictionary Count — represents reserved/known leases.
            return this.activeLeases.Count;
        }
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.LayoutRules", "SA1513:Closing brace should be followed by blank line", Justification = "not for property default value")]
    public OxygenWorld World
    {
        get
        {
            _ = this.EnsureIsReadyOrRunning();
            return field;
        }

        private set => field = value;
    }
    = null!; // will be initialized during engine initialization

    /// <inheritdoc/>
    public void MountProjectCookedRoot(string path)
    {
        _ = this.EnsureIsRunning();
        this.World.AddLooseCookedRoot(path);
    }

    /// <inheritdoc/>
    public void UnmountProjectCookedRoot()
    {
        _ = this.EnsureIsRunning();
        this.World.ClearCookedRoots();
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.LayoutRules", "SA1513:Closing brace should be followed by blank line", Justification = "not for property default value")]
    public OxygenInput Input
    {
        get
        {
            _ = this.EnsureIsReadyOrRunning();
            return field;
        }

        private set => field = value;
    }
    = null!; // will be initialized during engine initialization

    /// <inheritdoc />
    public async ValueTask<bool> InitializeAsync(CancellationToken cancellationToken = default)
    {
        await this.initializationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (this.state is not (EngineServiceState.NoEngine or EngineServiceState.Faulted))
            {
                this.LogAlreadyInitialized();
                return true; // No-op
            }

            if (this.State is EngineServiceState.Faulted)
            {
                // Clearing the faulted state is needed to allow re-initialization. We will try to
                // shutdown the engine, because it was not done cleanly before calling
                // InitializeAsync, but we must not fail if shutdown throws.
                await this.TryShutdownAsync().ConfigureAwait(false);
            }

            Debug.Assert(this.engineRunner == null, "Expecting engine runner to be null before initialization starts.");
            this.state = EngineServiceState.Initializing;
            this.engineRunner = new EngineRunner();
            if (loggerFactory is { } factory) // TODO: pass parameters to InitializeAsync to configure logging
            {
                var loggingConfig = new LoggingConfig
                {
                    Verbosity = 0,
                    IsColored = false,
                    ModuleOverrides = string.Empty, // "**/Renderer/*=0,**/*Interop/**/*=3,**/Graphics/**/Command*=0",
                };
                var engineLogger = factory.CreateLogger("Oxygen.Engine");
                _ = this.engineRunner.ConfigureLogging(loggingConfig, engineLogger);
            }

            this.LogRunnerInitialized();

            // Configure the engine for headless operation
            var config = ConfigFactory.CreateDefaultEngineConfig();
            config.TargetFps = 1; // TODO: remove after editor is stable
            config.Graphics ??= new GraphicsConfigManaged();
            config.Graphics.Headless = true;
            config.EnableAssetLoader = true;

            this.engineContext = this.engineRunner.CreateEngine(config);
            if (this.engineContext?.IsValid != true)
            {
                throw new InvalidOperationException("Failed to create engine context.");
            }

            this.World = new OxygenWorld(this.engineContext);
            this.Input = new OxygenInput(this.engineContext);

            this.LogContextReady();
            this.state = EngineServiceState.Ready;
            return true;
        }
        catch (Exception ex)
        {
            this.LogInitializationFailed(ex);
            await this.TryShutdownAsync().ConfigureAwait(false);
            throw;
        }
        finally
        {
            _ = this.initializationGate.Release();
        }
    }

    /// <inheritdoc />
    public async ValueTask StartAsync()
    {
        this.LogStartingEngineLoop();

        this.EnsureInStates(
           EngineServiceState.Ready,
           EngineServiceState.Starting,
           EngineServiceState.Running);

        if (this.state is EngineServiceState.Starting or EngineServiceState.Running)
        {
            return; // No-op
        }

        var runner = this.engineRunner;
        Debug.Assert(runner is not null, "Engine runner should be initialized when engine is ready.");

        this.state = EngineServiceState.Starting;

        // Spawn the engine loop task
        this.engineLoopTask = runner.RunEngineAsync(this.engineContext);

        this.state = EngineServiceState.Running;
    }

    /// <inheritdoc />
    public async ValueTask ShutdownAsync()
    {
        this.LogShutdownRequested();

        this.EnsureInStates(
           EngineServiceState.NoEngine,
           EngineServiceState.Ready,
           EngineServiceState.Running,
           EngineServiceState.Faulted);

        if (this.state is EngineServiceState.NoEngine)
        {
            return; // No-op
        }

        this.state = EngineServiceState.ShuttingDown;
        List<ViewportSurfaceLease> leasesSnapshot;
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            // Snapshot current leases. Do NOT clear here; let each lease's Dispose/Release
            // perform native unregister and internal removal. Clearing here previously could
            // leave native surfaces registered without a managed owner.
            leasesSnapshot = [.. this.activeLeases.Values];
        }
        finally
        {
            _ = this.leaseGate.Release();
        }

        // Dispose each lease which will attempt to unregister the native surface and
        // remove itself from the collections in a safe, synchronized manner. DisposeAsync
        // is implemented to never throw, so callers should not wrap it in try/catch.
        foreach (var lease in leasesSnapshot)
        {
            await lease.DisposeAsync().ConfigureAwait(false);
        }

        try
        {
            await this.TryStopEngineAsync().ConfigureAwait(false);
            this.TryDestroyEngineContext();
        }
        finally
        {
            this.engineRunner?.Dispose();
            this.engineRunner = null;
        }

        this.state = EngineServiceState.NoEngine;
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        await this.TryShutdownAsync().ConfigureAwait(false);
        this.initializationGate.Dispose();
        this.leaseGate.Dispose();
    }

    private async ValueTask TryStopEngineAsync()
    {
        var runner = this.EnsureIsRunning();

        Debug.Assert(this.engineLoopTask is not null, "Engine loop task should be active when engine is running.");
        Debug.Assert(this.engineContext is not null, "Engine context should be valid when engine is running.");

        this.LogStoppingEngineLoop();
        try
        {
            runner.StopEngine(this.engineContext);
            if (this.engineLoopTask != null)
            {
                await this.engineLoopTask.ConfigureAwait(false);
            }
        }
        finally
        {
            this.engineLoopTask = null;
            this.state = EngineServiceState.Ready;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "must not throw")]
    private async ValueTask TryShutdownAsync()
    {
        // Defensively try to shutdown the engine. Shutdown is idempotent, can
        // be called in any state, and will have no effect if the engine was
        // already shutdown.
        try
        {
            await this.ShutdownAsync().ConfigureAwait(false);
        }
        catch
        {
            // Swallow exceptions because we are in a best-effort cleanup path.
        }
        finally
        {
            this.state = EngineServiceState.NoEngine;
        }
    }

    private EngineRunner EnsureIsReadyOrRunning()
    {
        var validStates = new EngineServiceState[] { EngineServiceState.Ready, EngineServiceState.Running };
        this.EnsureInStates(validStates);

        Debug.Assert(this.engineRunner is not null, $"Engine runner should be initialized when state is in [{string.Join(", ", validStates)}].");
        return this.engineRunner;
    }

    private EngineRunner EnsureIsRunning()
    {
        var validStates = new EngineServiceState[] { EngineServiceState.Running };
        this.EnsureInStates(validStates);

        Debug.Assert(this.engineRunner is not null, $"Engine runner should be initialized when state is in [{string.Join(", ", validStates)}].");
        return this.engineRunner;
    }

    private void EnsureInStates(params EngineServiceState[] validStates)
    {
        if (Array.IndexOf(validStates, this.state) < 0)
        {
            var message = $"Engine must be in state: {string.Join(", ", validStates)}. Current state: {this.state}.";
            Debug.Fail(message);
            throw new InvalidOperationException(message);
        }
    }

    private void EnsureOnDispatcherThread()
    {
        if (!this.hostingContext.Dispatcher.HasThreadAccess)
        {
            throw new InvalidOperationException("Engine operations must be performed on the UI dispatcher thread.");
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "in the dispose path, cannot throw")]
    private void TryDestroyEngineContext()
    {
        if (this.engineContext == null)
        {
            return;
        }

        try
        {
            this.engineContext.Dispose();
        }
        catch (Exception ex)
        {
            this.LogInitializationFailed(ex);
        }
        finally
        {
            this.engineContext = null;
            this.World = null!;
            this.Input = null!;
            this.LogContextDestroyed();
        }
    }
}
