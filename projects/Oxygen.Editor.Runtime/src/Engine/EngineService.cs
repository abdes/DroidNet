// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Interop;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

// TODO: engine config options for InitializeAsync to be exposed via EngineService
// TODO: auto-tune the engine target FPS based on whether we have active views to render or not

/// <summary>
///     Application-wide coordinator that keeps the native engine alive and arbitrates access to
///     presentation surfaces and views used by the editor.
/// </summary>
/// <param name="hostingContext">Provides access to the UI dispatcher context.</param>
/// <param name="operationResults">Publishes structured runtime lifetime failures.</param>
/// <param name="loggerFactory">Optional factory used to bridge native engine logging.</param>
/// <param name="engineSettings">Editor native engine startup settings.</param>
/// <param name="pathFinder">Resolves editor configuration paths.</param>
public sealed partial class EngineService(
    HostingContext hostingContext,
    IOperationResultPublisher operationResults,
    ILoggerFactory? loggerFactory = null,
    ISettingsService<IEngineSettings>? engineSettings = null,
    IPathFinder? pathFinder = null) : IEngineService
{
    private const string EngineDefaultCVarsArchivePath = "bin/Oxygen/cvars.json";
    private const string EditorCVarsArchiveFileName = "engine-cvars.json";

    private readonly HostingContext hostingContext = hostingContext;
    private readonly IOperationResultPublisher operationResults = operationResults;
    private readonly IEngineSettings engineSettings = engineSettings?.Settings ?? new EngineSettings();
    private readonly IPathFinder? pathFinder = pathFinder;
    private readonly ILogger<EngineService> logger = loggerFactory?.CreateLogger<EngineService>() ?? NullLoggerFactory.Instance.CreateLogger<EngineService>();

    // This gate has no wait handles and remains available for concurrent/repeated cleanup.
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "Cleanup remains callable after disposal; SemaphoreSlim.AvailableWaitHandle is never used.")]
    private readonly SemaphoreSlim lifecycleGate = new(1, 1);
    private readonly Func<EngineSession> sessionFactory = () => new NativeEngineSession(hostingContext);
    private readonly System.Collections.Concurrent.ConcurrentDictionary<Guid, int> documentSurfaceCounts = new();
    private readonly System.Collections.Concurrent.ConcurrentDictionary<ViewportSurfaceKey, ViewportSurfaceLease> activeLeases = new();
    private readonly RuntimeCommandDispatcher commandDispatcher = new();
    private int reservedSurfaceCount;

    private EngineSession? session;
    private Task? engineLoopTask;
    private volatile EngineServiceState state = EngineServiceState.NoEngine;
    private volatile bool disposalRequested;

    /// <summary>Initializes a new instance of the <see cref="EngineService"/> class with a native ownership factory.</summary>
    /// <param name="hostingContext">The UI context.</param>
    /// <param name="sessionFactory">Creates the native ownership boundary.</param>
    /// <param name="operationResults">The runtime diagnostics publisher.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    internal EngineService(HostingContext hostingContext, Func<EngineSession> sessionFactory, IOperationResultPublisher operationResults, ILoggerFactory? loggerFactory = null)
        : this(hostingContext, operationResults, loggerFactory)
    {
        this.sessionFactory = sessionFactory;
    }

    /// <inheritdoc/>
    public event EventHandler<EngineStateChangedEventArgs>? StateChanged;

    /// <inheritdoc/>
    public EngineServiceState State => this.state is EngineServiceState.Running && this.engineLoopTask?.IsCompleted == true
        ? EngineServiceState.Faulted
        : this.state;

    /// <inheritdoc/>
    public int EngineLoggingVerbosity
    {
        get
        {
            var runner = this.EnsureIsReadyOrRunning();
            var cfg = runner.GetLoggingConfig(this.EngineContext);
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
                var cfg = runner.GetLoggingConfig(this.EngineContext);
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
            var cfg = runner.GetEngineConfig(this.EngineContext);
            Debug.Assert(cfg is not null, "A ready or running engine should return a valid EngineConfig object");
            return cfg.TargetFps;
        }

        set
        {
            var runner = this.EnsureIsReadyOrRunning();
            var clamped = Math.Clamp(value, 0, this.MaxTargetFps);
            runner.SetTargetFps(this.EngineContext, clamped);
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

    /// <inheritdoc/>
    public IRuntimeWorldCommands WorldCommands => this.commandDispatcher;

    /// <inheritdoc/>
    public IRuntimeInputCommands InputCommands => this.commandDispatcher;

    private EngineContext? EngineContext => this.session?.Context;

    /// <inheritdoc/>
    public void MountProjectCookedRoot(string path)
    {
        _ = this.EnsureIsRunning();
        this.session!.Commands.MountCookedRoot(path);
    }

    /// <inheritdoc/>
    public void UnmountProjectCookedRoot()
    {
        _ = this.EnsureIsRunning();
        this.session!.Commands.ClearCookedRoots();
    }

    private static bool ShouldUseEditorCVarsArchive(string? cvarsArchivePath)
        => string.IsNullOrWhiteSpace(cvarsArchivePath)
            || string.Equals(
                cvarsArchivePath.Replace('\\', '/'),
                EngineDefaultCVarsArchivePath,
                StringComparison.OrdinalIgnoreCase);

    private EngineRunner EnsureIsReadyOrRunning()
    {
        this.EnsureInStates(EngineServiceState.Ready, EngineServiceState.Running);
        return this.session!.Runner;
    }

    private EngineRunner EnsureIsRunning()
    {
        this.EnsureInStates(EngineServiceState.Running);
        return this.session!.Runner;
    }

    private void ApplyEditorRuntimePathDefaults(EditorEngineConfigManaged config)
    {
        if (this.pathFinder is null)
        {
            return;
        }

        var editorCVarsArchivePath = this.pathFinder.GetConfigFilePath(EditorCVarsArchiveFileName);
        config.Engine ??= new EngineConfig();
        config.Renderer ??= new RendererConfigManaged();
        config.Engine.PathFinder ??= new PathFinderConfigManaged();
        if (ShouldUseEditorCVarsArchive(config.Engine.PathFinder.CVarsArchivePath))
        {
            config.Engine.PathFinder.CVarsArchivePath = editorCVarsArchivePath;
        }

        config.Renderer.PathFinder ??= config.Engine.PathFinder;
        if (ShouldUseEditorCVarsArchive(config.Renderer.PathFinder.CVarsArchivePath))
        {
            config.Renderer.PathFinder.CVarsArchivePath = editorCVarsArchivePath;
        }
    }

    private void EnsureInStates(params EngineServiceState[] validStates)
    {
        ObjectDisposedException.ThrowIf(this.disposalRequested, this);
        if (Array.IndexOf(validStates, this.State) < 0)
        {
            var message = $"Engine must be in state: {string.Join(", ", validStates)}. Current state: {this.State}.";
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
}
