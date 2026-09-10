// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Serialized initialization and ownership-aware teardown.</summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public async ValueTask<bool> InitializeAsync(CancellationToken cancellationToken = default)
    {
        await this.lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            ObjectDisposedException.ThrowIf(this.disposalRequested, this);
            if (this.State is EngineServiceState.Ready or EngineServiceState.Running)
            {
                return true;
            }

            ThrowCleanupFailures(await this.ShutdownCoreAsync().ConfigureAwait(true));
            return await this.InitializeCoreAsync().ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async ValueTask StartAsync()
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            this.EnsureInStates(EngineServiceState.Ready, EngineServiceState.Running);
            if (this.State is EngineServiceState.Running)
            {
                return;
            }

            this.state = EngineServiceState.Starting;
            this.LogStartingEngineLoop();
            try
            {
                this.engineLoopTask = this.session!.RunAsync();
                this.commandDispatcher.BeginRun(this.session.Commands, this.engineLoopTask);
                this.state = EngineServiceState.Running;
            }
            catch
            {
                this.state = EngineServiceState.Faulted;
                _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
                throw;
            }
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async ValueTask ShutdownAsync()
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            ThrowCleanupFailures(await this.ShutdownCoreAsync().ConfigureAwait(true));
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async ValueTask DisposeAsync()
    {
        this.disposalRequested = true;
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    private static void ThrowCleanupFailures(List<Exception> failures)
    {
        if (failures.Count > 0)
        {
            throw new AggregateException("Engine shutdown encountered failures. Inspect State before attempting to restart.", failures);
        }
    }

    private async Task<bool> InitializeCoreAsync()
    {
        this.state = EngineServiceState.Initializing;
        try
        {
            this.session = this.sessionFactory();
            var config = ConfigFactory.CreateDefaultEditorEngineConfig();
            config.Engine.TargetFps = 1;
            this.engineSettings.ApplyTo(config);
            this.ApplyEditorRuntimePathDefaults(config);
            config.Platform.Headless = true;
            config.Engine.Graphics.Headless = true;
            config.Engine.EnableAssetLoader = true;
            this.session.Initialize(config, loggerFactory?.CreateLogger("Oxygen.Engine"));
            this.state = EngineServiceState.Ready;
            this.LogContextReady();
            return true;
        }
        catch
        {
            _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
            throw;
        }
    }

    private async Task<List<Exception>> ShutdownCoreAsync()
    {
        List<Exception> failures = [];
        if (this.session is null)
        {
            this.state = EngineServiceState.NoEngine;
            return failures;
        }

        this.commandDispatcher.EndRun();
        this.state = EngineServiceState.ShuttingDown;
        this.LogShutdownRequested();
        foreach (var lease in this.activeLeases.Values.ToArray())
        {
            _ = await this.TryCleanupAsync(() => this.ReleaseLeaseCoreAsync(lease), "Release surface", failures).ConfigureAwait(true);
        }

        if (!await this.StopLoopAsync(failures).ConfigureAwait(true))
        {
            this.state = EngineServiceState.Faulted;
            return failures;
        }

        _ = this.TryCleanup(this.session.DestroyRunner, "Destroy runner", failures);
        _ = this.TryCleanup(this.session.DestroyContext, "Destroy context", failures);
        if (this.session.HasRunner || this.session.HasContext)
        {
            this.state = EngineServiceState.Faulted;
            return failures;
        }

        foreach (var lease in this.activeLeases.Values)
        {
            lease.MarkReleased();
        }

        this.activeLeases.Clear();
        this.documentSurfaceCounts.Clear();
        this.orphanedViewportIds.Clear();
        this.reservedSurfaceCount = 0;
        this.session = null;
        this.state = EngineServiceState.NoEngine;
        return failures;
    }

    private async Task<bool> StopLoopAsync(List<Exception> failures)
    {
        if (this.engineLoopTask is null)
        {
            return true;
        }

        if (!this.engineLoopTask.IsCompleted && !this.TryCleanup(this.session!.Stop, "Stop loop", failures))
        {
            return false;
        }

        _ = await this.TryCleanupAsync(() => this.engineLoopTask, "Await loop", failures).ConfigureAwait(true);

        // Native loop completion precedes the dispatcher callback that releases surface tokens.
        if (!await this.TryCleanupAsync(this.session!.CompleteLoopCleanupAsync, "Complete loop cleanup", failures).ConfigureAwait(true))
        {
            return false;
        }

        this.engineLoopTask = null;
        return true;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Each native cleanup failure is recorded while independent resources continue cleanup.")]
    private bool TryCleanup(Action cleanup, string stage, List<Exception> failures)
    {
        try
        {
            cleanup();
            return true;
        }
        catch (Exception exception)
        {
            failures.Add(exception);
            this.LogCleanupFailure(exception, stage);
            return false;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Each native cleanup failure is recorded while independent resources continue cleanup.")]
    private async Task<bool> TryCleanupAsync(Func<Task> cleanup, string stage, List<Exception> failures)
    {
        try
        {
            await cleanup().ConfigureAwait(true);
            return true;
        }
        catch (Exception exception)
        {
            failures.Add(exception);
            this.LogCleanupFailure(exception, stage);
            return false;
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Engine cleanup failed during {Stage}.")]
    private partial void LogCleanupFailure(Exception exception, string stage);
}
