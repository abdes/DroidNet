// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Observes loop completion without taking over native cleanup ownership.</summary>
public sealed partial class EngineService
{
    private readonly ConcurrentQueue<EngineStateChangedEventArgs> stateChanges = new();
    private EngineRun? currentRun;
    private int publishingStateChanges;

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The loop lifetime boundary observes every task failure and preserves the original exception for diagnostics and cleanup.")]
    private static async Task<LoopCompletion> CaptureLoopCompletionAsync(Task loop)
    {
        try
        {
            await loop.ConfigureAwait(false);
            return new(Exception: null, DateTimeOffset.UtcNow);
        }
        catch (Exception exception)
        {
            return new(exception, DateTimeOffset.UtcNow);
        }
    }

    private static OperationResult CreateLoopFailure(EngineRun run, LoopCompletion completion)
    {
        var exception = completion.Exception;
        var message = exception?.Message ?? "The engine loop exited without a shutdown request.";
        return new OperationResult
        {
            OperationId = run.Id,
            OperationKind = RuntimeOperationKinds.Loop,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Engine preview stopped unexpectedly",
            Message = message,
            StartedAt = run.StartedAt,
            CompletedAt = completion.CompletedAt,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = run.Id,
                    Domain = FailureDomain.RuntimeExecution,
                    Severity = DiagnosticSeverity.Error,
                    Code = exception is null ? RuntimeDiagnosticCodes.LoopExited : RuntimeDiagnosticCodes.LoopFaulted,
                    Message = message,
                    TechnicalMessage = exception?.ToString(),
                    ExceptionType = exception?.GetType().FullName,
                },
            ],
        };
    }

    private async Task ObserveLoopAsync(EngineRun run)
    {
        var completion = await run.Completion.ConfigureAwait(false);
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            this.ObserveLoopCompletion(run, completion);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
            this.PublishStateChanges();
        }
    }

    // Called only under lifecycleGate, both by the active observer and by shutdown.
    // Shutdown consumes the same captured completion before clearing the run so
    // it cannot hide a failure from an observer waiting for this gate.
    private void ObserveLoopCompletion(EngineRun run, LoopCompletion completion)
    {
        if (!ReferenceEquals(this.currentRun, run) || run.CompletionObserved)
        {
            return;
        }

        run.CompletionObserved = true;
        this.commandDispatcher.EndRun();
        if (run.StopRequested && completion.Exception is null or OperationCanceledException)
        {
            return;
        }

        var result = CreateLoopFailure(run, completion);
        this.ChangeState(EngineServiceState.Faulted, result, completion.Exception);
    }

    private void ChangeState(EngineServiceState next, OperationResult? result = null, Exception? exception = null)
    {
        var previous = this.state;
        if (previous == next && result is null)
        {
            return;
        }

        this.state = next;
        this.stateChanges.Enqueue(new(this.currentRun?.Id ?? Guid.Empty, previous, next, result, exception));
    }

    private void PublishStateChanges()
    {
        do
        {
            if (Interlocked.CompareExchange(ref this.publishingStateChanges, 1, 0) != 0)
            {
                return;
            }

            try
            {
                while (this.stateChanges.TryDequeue(out var change))
                {
                    this.PublishStateChange(change);
                }
            }
            finally
            {
                Volatile.Write(ref this.publishingStateChanges, 0);
            }
        }
        while (!this.stateChanges.IsEmpty);
    }

    private void PublishStateChange(EngineStateChangedEventArgs change)
    {
        if (change.OperationResult is { } result)
        {
            if (string.Equals(result.OperationKind, "Runtime.Qualification", StringComparison.Ordinal))
            {
                this.LogQualificationFailed(result.Message, change.Exception);
            }
            else
            {
                this.LogLoopTerminated(change.RunId, result.Message, change.Exception);
            }

            this.NotifySubscriber(() => this.operationResults.Publish(result));
        }

        if (this.StateChanged is { } handlers)
        {
            foreach (EventHandler<EngineStateChangedEventArgs> handler in handlers.GetInvocationList())
            {
                this.NotifySubscriber(() => handler(this, change));
            }
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A diagnostics or lifecycle subscriber must not fault the loop observer, suppress other consumers, or interrupt native cleanup.")]
    private void NotifySubscriber(Action notify)
    {
        try
        {
            notify();
        }
        catch (Exception exception)
        {
            this.LogStateNotificationFailed(exception);
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Engine run {RunId} terminated unexpectedly: {Reason}")]
    private partial void LogLoopTerminated(Guid runId, string reason, Exception? exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "A runtime lifecycle notification consumer failed.")]
    private partial void LogStateNotificationFailed(Exception exception);

    private sealed class EngineRun(Guid id, Task loop)
    {
        public Guid Id { get; } = id;

        public DateTimeOffset StartedAt { get; } = DateTimeOffset.UtcNow;

        public Task<LoopCompletion> Completion { get; } = CaptureLoopCompletionAsync(loop);

        public bool StopRequested { get; set; }

        public bool CompletionObserved { get; set; }
    }

    private sealed record LoopCompletion(Exception? Exception, DateTimeOffset CompletedAt);
}
