// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Serialized initialization and ownership-aware teardown.</summary>
public sealed partial class EngineService
{
    private readonly Lock startupGate = new();
    [SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "Non-owning reference: StartAsync disposes the token before releasing lifecycleGate, which DisposeAsync awaits.")]
    private CancellationTokenSource? startupCancellation;
    private int shutdownRequests;

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
            return await this.InitializeCoreAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
            this.PublishStateChanges();
        }
    }

    /// <inheritdoc/>
    public async ValueTask StartAsync()
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            CancellationTokenSource startup;
            lock (this.startupGate)
            {
                this.EnsureInStates(EngineServiceState.Ready, EngineServiceState.Running);
                if (this.shutdownRequests != 0)
                {
                    throw new OperationCanceledException("Engine shutdown has been requested.");
                }

                if (this.State is EngineServiceState.Running)
                {
                    return;
                }

                startup = new CancellationTokenSource();
                this.startupCancellation = startup;
            }

            try
            {
                this.ChangeState(EngineServiceState.Starting);
                this.LogStartingEngineLoop();
                startup.Token.ThrowIfCancellationRequested();
                this.engineLoopTask = this.session!.RunAsync();
                await this.session.WaitForStartupAsync().WaitAsync(startup.Token).ConfigureAwait(true);
                startup.Token.ThrowIfCancellationRequested();
                var runId = this.commandDispatcher.BeginRun(this.session.Commands, this.engineLoopTask);
                this.currentRun = new(runId, this.engineLoopTask);
                this.ChangeState(EngineServiceState.Running);
                _ = this.ObserveLoopAsync(this.currentRun);
            }
            catch
            {
                this.ChangeState(EngineServiceState.Faulted);
                _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
                throw;
            }
            finally
            {
                lock (this.startupGate)
                {
                    this.startupCancellation = null;
                    startup.Dispose();
                }
            }
        }
        finally
        {
            _ = this.lifecycleGate.Release();
            this.PublishStateChanges();
        }
    }

    /// <inheritdoc/>
    public async ValueTask ShutdownAsync()
    {
        this.RequestShutdown();
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            ThrowCleanupFailures(await this.ShutdownCoreAsync().ConfigureAwait(true));
        }
        finally
        {
            _ = this.lifecycleGate.Release();
            this.CompleteShutdownRequest();
            this.PublishStateChanges();
        }
    }

    /// <inheritdoc/>
    public async ValueTask DisposeAsync()
    {
        this.disposalRequested = true;
        this.RequestShutdown();
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
            this.CompleteShutdownRequest();
            this.PublishStateChanges();
        }
    }

    private static void ThrowCleanupFailures(List<Exception> failures)
    {
        if (failures.Count > 0)
        {
            throw new AggregateException("Engine shutdown encountered failures. Inspect State before attempting to restart.", failures);
        }
    }

    private async Task<bool> InitializeCoreAsync(CancellationToken cancellationToken)
    {
        this.ChangeState(EngineServiceState.Initializing);
        try
        {
            this.session = await this.sessionFactory(cancellationToken).ConfigureAwait(true);
            this.session.Initialize(this.engineSettings, this.pathFinder?.GetConfigFilePath(EditorCVarsArchiveFileName), loggerFactory?.CreateLogger("Oxygen.Engine"));
            this.ChangeState(EngineServiceState.Ready);
            this.LogContextReady();
            return true;
        }
        catch (NativeCompatibilityException exception)
        {
            _ = await this.ShutdownCoreAsync().ConfigureAwait(true);
            var result = new OperationResult
            {
                OperationId = exception.Diagnostics.FirstOrDefault()?.OperationId ?? Guid.NewGuid(),
                OperationKind = "Runtime.Compatibility",
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Error,
                Title = "Native runtime unavailable",
                Message = exception.Message,
                Diagnostics = exception.Diagnostics,
            };
            this.ChangeState(EngineServiceState.Faulted, result, exception);
            throw;
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
            this.ChangeState(EngineServiceState.NoEngine);
            return failures;
        }

        this.commandDispatcher.EndRun();
        if (this.currentRun is { } run && this.engineLoopTask is { IsCompleted: false })
        {
            run.StopRequested = true;
        }

        this.ChangeState(EngineServiceState.ShuttingDown);
        this.LogShutdownRequested();
        foreach (var lease in this.activeLeases.Values.ToArray())
        {
            _ = await this.TryCleanupAsync(() => this.ReleaseLeaseCoreAsync(lease), "Release surface", failures).ConfigureAwait(true);
        }

        if (!await this.StopLoopAsync(failures).ConfigureAwait(true))
        {
            this.ChangeState(EngineServiceState.Faulted);
            return failures;
        }

        _ = this.TryCleanup(this.session.DestroyRunner, "Destroy runner", failures);
        _ = this.TryCleanup(this.session.DestroyContext, "Destroy context", failures);
        if (this.session.HasRunner || this.session.HasContext)
        {
            this.ChangeState(EngineServiceState.Faulted);
            return failures;
        }

        _ = this.TryCleanup(this.ReleaseCookedContentReaders, "Release cooked content readers", failures);
        if (this.cookedContentReaders.Count != 0)
        {
            this.ChangeState(EngineServiceState.Faulted);
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
        this.ChangeState(EngineServiceState.NoEngine);
        this.currentRun = null;
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

        _ = await this.TryCleanupAsync(this.AwaitLoopForShutdownAsync, "Await loop", failures).ConfigureAwait(true);
        if (this.currentRun is { } run)
        {
            this.ObserveLoopCompletion(run, await run.Completion.ConfigureAwait(true));
        }

        // Native loop completion precedes the dispatcher callback that releases surface tokens.
        if (!await this.TryCleanupAsync(this.session!.CompleteLoopCleanupAsync, "Complete loop cleanup", failures).ConfigureAwait(true))
        {
            return false;
        }

        this.engineLoopTask = null;
        return true;
    }

    private void RequestShutdown()
    {
        lock (this.startupGate)
        {
            this.shutdownRequests++;
            this.startupCancellation?.Cancel();
        }
    }

    private void CompleteShutdownRequest()
    {
        lock (this.startupGate)
        {
            this.shutdownRequests--;
        }
    }

    private async Task AwaitLoopForShutdownAsync()
    {
        try
        {
            await this.engineLoopTask!.ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (this.currentRun?.StopRequested == true)
        {
            // Cancellation following our stop request is ordinary loop completion.
        }
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
