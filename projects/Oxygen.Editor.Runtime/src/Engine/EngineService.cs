// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Core.Compatibility;
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
/// <param name="artifactQualification">Verifies the fixed installed artifact set before native loading.</param>
public sealed partial class EngineService(
    HostingContext hostingContext,
    IOperationResultPublisher operationResults,
    ILoggerFactory? loggerFactory = null,
    ISettingsService<IEngineSettings>? engineSettings = null,
    IPathFinder? pathFinder = null,
    IArtifactQualificationService? artifactQualification = null) : IEngineService
{
    private const string EditorCVarsArchiveFileName = "engine-cvars.json";

    private readonly HostingContext hostingContext = hostingContext;
    private readonly IOperationResultPublisher operationResults = operationResults;
    private readonly IEngineSettings engineSettings = engineSettings?.Settings ?? new EngineSettings();
    private readonly IPathFinder? pathFinder = pathFinder;
    private readonly ILogger<EngineService> logger = loggerFactory?.CreateLogger<EngineService>() ?? NullLoggerFactory.Instance.CreateLogger<EngineService>();

    // This gate has no wait handles and remains available for concurrent/repeated cleanup.
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "Cleanup remains callable after disposal; SemaphoreSlim.AvailableWaitHandle is never used.")]
    private readonly SemaphoreSlim lifecycleGate = new(1, 1);
    private readonly Func<CancellationToken, Task<EngineSession>> sessionFactory = token => CreateQualifiedSessionAsync(hostingContext, artifactQualification ?? EditorArtifactQualificationService.ForCurrentProcess(), token);
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
        this.sessionFactory = _ => Task.FromResult(sessionFactory());
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
        get => this.EnsureIsReadyOrRunning().LoggingVerbosity;

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
                runner.LoggingVerbosity = value;

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
    public uint MaxTargetFps => this.EnsureIsReadyOrRunning().MaxTargetFps;

    /// <inheritdoc/>
    public uint TargetFps
    {
        get => this.EnsureIsReadyOrRunning().TargetFps;

        set
        {
            var runner = this.EnsureIsReadyOrRunning();
            var clamped = Math.Clamp(value, 0, this.MaxTargetFps);
            runner.TargetFps = clamped;
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

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "The factory signature stays on the managed boundary so service construction does not require the native session type.")]
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static EngineSession CreateNativeSession(HostingContext hostingContext, QualifiedArtifactLease artifacts) => new NativeEngineSession(hostingContext, artifacts);

    private static async Task<EngineSession> CreateQualifiedSessionAsync(HostingContext hostingContext, IArtifactQualificationService qualification, CancellationToken cancellationToken)
    {
        var result = await qualification.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        if (!result.Succeeded)
        {
            throw new ArtifactQualificationException(result.Diagnostics);
        }

        var artifacts = result.Artifacts!;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var interopPath = Path.Combine(AppContext.BaseDirectory, "DroidNet.Oxygen.Editor.Interop.dll");
            if (!string.Equals(artifacts.GetPath(EditorArtifactInventory.InteropId), interopPath, StringComparison.OrdinalIgnoreCase))
            {
                throw new ArtifactQualificationException(
                [
                    new DiagnosticRecord
                    {
                        OperationId = Guid.NewGuid(),
                        Domain = FailureDomain.RuntimeDiscovery,
                        Severity = DiagnosticSeverity.Error,
                        Code = ArtifactQualificationDiagnosticCodes.ArtifactMismatch,
                        Message = "Qualification refers to a different editor Interop assembly.",
                        AffectedPath = interopPath,
                    },
                ]);
            }

            var directory = Path.GetDirectoryName(artifacts.GetPath(EditorArtifactInventory.RuntimeId(EditorArtifactQualificationService.CurrentConfiguration)))!;
            var path = Environment.GetEnvironmentVariable("PATH");
            if (path?.Split(Path.PathSeparator).Contains(directory, StringComparer.OrdinalIgnoreCase) != true)
            {
                Environment.SetEnvironmentVariable("PATH", string.IsNullOrEmpty(path) ? directory : directory + Path.PathSeparator + path);
            }

            return CreateNativeSession(hostingContext, artifacts);
        }
        catch
        {
            await artifacts.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    private EngineSession EnsureIsReadyOrRunning()
    {
        this.EnsureInStates(EngineServiceState.Ready, EngineServiceState.Running);
        return this.session!;
    }

    private EngineSession EnsureIsRunning()
    {
        this.EnsureInStates(EngineServiceState.Running);
        return this.session!;
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
