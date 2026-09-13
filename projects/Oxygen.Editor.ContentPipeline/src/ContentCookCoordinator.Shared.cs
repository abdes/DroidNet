// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Shares pending scopes without transferring one caller's cancellation to other owners.</summary>
public sealed partial class ContentCookCoordinator
{
    private readonly Dictionary<SharedCookKey, SharedCook> pendingShared = [];

    private Task<T> RunSharedAsync<T>(CookRunRequest request, Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(work);
        cancellationToken.ThrowIfCancellationRequested();
        SharedCook<T> shared;
        var start = false;
        CookRunSnapshot? promoted = null;
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            var key = new SharedCookKey(this.lifetime, request.TargetKind, request.ScopeUri, typeof(T), request.OriginContext);
            if (this.pendingShared.TryGetValue(key, out var existing) && !existing.Cancellation.IsCancellationRequested
                && (!this.runs.TryGetValue(existing.OperationId, out var run) || !run.Cancellation.IsCancellationRequested))
            {
                shared = (SharedCook<T>)existing;
                promoted = this.PromoteShared(shared, request);
            }
            else
            {
                shared = new(key, request);
                this.pendingShared[key] = shared;
                start = true;
            }

            shared.Owners++;
        }

        if (promoted is not null)
        {
            this.PublishRun(promoted, reveal: !promoted.Request.IsAutomatic);
        }

        var observation = this.ObserveSharedAsync(shared, cancellationToken);
        if (start)
        {
            _ = this.ExecuteSharedAsync(shared, work);
        }

        return observation;
    }

    private CookRunSnapshot? PromoteShared(SharedCook shared, CookRunRequest incoming)
    {
        var promoted = shared.Request with
        {
            IsAutomatic = shared.Request.IsAutomatic && incoming.IsAutomatic,
            IsDemand = shared.Request.IsDemand || incoming.IsDemand,
        };
        if (promoted == shared.Request)
        {
            return null;
        }

        shared.Request = promoted;
        if (!this.runs.TryGetValue(shared.OperationId, out var run))
        {
            return null;
        }

        this.revealQueueOutcome |= !promoted.IsAutomatic;
        run.Snapshot = Append(run.Snapshot with { Request = promoted }, new(Message: incoming.IsAutomatic
            ? "Active preview also needs this queued cook." : "Explicit cook joined this queued scope."));
        this.DispatchNextWriter();
        return run.Snapshot;
    }

    private async Task<T> ObserveSharedAsync<T>(SharedCook<T> shared, CancellationToken cancellationToken)
    {
        try
        {
            return await shared.Completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Task? cancel = null;
            lock (this.stateLock)
            {
                shared.Owners--;
                if (shared.Owners == 0 && !shared.Finished)
                {
                    this.RemovePendingShared(shared);
                    cancel = shared.CancellationCompletion = shared.Cancellation.CancelAsync();
                }
            }

            if (cancel is not null)
            {
                await cancel.ConfigureAwait(false);
            }
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Forward operation failures to all observers and log cancellation callback failures; detached observers cannot observe this owner task.")]
    private async Task ExecuteSharedAsync<T>(SharedCook<T> shared, Func<ContentCookOperation, CancellationToken, Task<T>> work)
    {
        try
        {
            var result = await this.RunCoreAsync(work, shared.Request, shared.Cancellation.Token, shared).ConfigureAwait(false);
            _ = shared.Completion.TrySetResult(result);
        }
        catch (OperationCanceledException exception)
        {
            _ = shared.Completion.TrySetCanceled(exception.CancellationToken);
        }
        catch (Exception exception)
        {
            _ = shared.Completion.TrySetException(exception);
            _ = shared.Completion.Task.Exception;
        }
        finally
        {
            Task? cancellation;
            lock (this.stateLock)
            {
                shared.Finished = true;
                this.RemovePendingShared(shared);
                cancellation = shared.CancellationCompletion;
            }

            try
            {
                if (cancellation is not null)
                {
                    await cancellation.ConfigureAwait(false);
                }
            }
            catch (Exception exception)
            {
                this.LogLifetimeFailure(exception);
            }
            finally
            {
                shared.Cancellation.Dispose();
            }
        }
    }

    private void RemovePendingShared(SharedCook shared)
    {
        if (this.pendingShared.TryGetValue(shared.Key, out var current) && ReferenceEquals(current, shared))
        {
            _ = this.pendingShared.Remove(shared.Key);
        }
    }

    private sealed record SharedCookKey(long Lifetime, CookTargetKind Kind, Uri? Scope, Type ResultType, Projects.ProjectContext? Origin)
    {
        public bool Equals(SharedCookKey? other)
            => other is not null && this.Lifetime == other.Lifetime && this.Kind == other.Kind && this.Scope == other.Scope
                && this.ResultType == other.ResultType && ReferenceEquals(this.Origin, other.Origin);

        public override int GetHashCode()
            => HashCode.Combine(
                this.Lifetime,
                this.Kind,
                this.Scope,
                this.ResultType,
                this.Origin is null ? 0 : System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this.Origin));
    }

    private abstract class SharedCook(SharedCookKey key, CookRunRequest request)
    {
        public SharedCookKey Key { get; } = key;

        public CookRunRequest Request { get; set; } = request;

        public CancellationTokenSource Cancellation { get; } = new();

        public Task? CancellationCompletion { get; set; }

        public Guid OperationId { get; set; }

        public int Owners { get; set; }

        public bool Finished { get; set; }
    }

    private sealed class SharedCook<T>(SharedCookKey key, CookRunRequest request) : SharedCook(key, request)
    {
        public TaskCompletionSource<T> Completion { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
