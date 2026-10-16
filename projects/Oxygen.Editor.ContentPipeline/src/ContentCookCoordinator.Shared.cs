// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Shares pending scopes without transferring one caller's cancellation to other owners.</summary>
/// <remarks>
/// A request joins a pending cook of the same scope, or one whose scope covers it: a folder covers what is under it and
/// the project covers everything. A run that has started is never joined. When a foreground request joins a pending
/// automatic cook of the same scope, the cook does the foreground request's work, which includes all of the automatic work.
/// </remarks>
public sealed partial class ContentCookCoordinator
{
    private readonly Dictionary<SharedCookKey, SharedCook> pendingShared = [];

    private static bool Covers(CookRunRequest pending, CookRunRequest incoming)
    {
        if (pending.IsReimport || incoming.IsReimport)
        {
            return false;
        }

        if (pending.TargetKind == CookTargetKind.Project)
        {
            return true;
        }

        if (pending.TargetKind != CookTargetKind.Folder || pending.ScopeUri is not { } folder || incoming.ScopeUri is not { } scope
            || !string.Equals(folder.Scheme, scope.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var root = folder.AbsolutePath.TrimEnd('/') + "/";
        return scope.AbsolutePath.StartsWith(root, StringComparison.OrdinalIgnoreCase);
    }

    private static string DescribeScope(CookRunRequest request)
        => request.ScopeUri is { } scope ? Uri.UnescapeDataString(scope.AbsolutePath) : "the project";

    private Task<T> RunSharedAsync<T>(CookRunRequest request, Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(work);
        cancellationToken.ThrowIfCancellationRequested();
        SharedCook<T> shared;
        var start = false;
        CookRunSnapshot? joined = null;
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            var key = new SharedCookKey(this.lifetime, request.TargetKind, request.ScopeUri, typeof(T), request.OriginContext);
            if (this.FindPendingShared(key, request) is SharedCook<T> existing)
            {
                shared = existing;
                if (shared.Key == key && shared.Request.IsAutomatic && !request.IsAutomatic)
                {
                    shared.Work = work;
                }

                joined = this.JoinShared(shared, request);
            }
            else
            {
                shared = new(key, request, work);
                this.pendingShared[key] = shared;
                start = true;
            }

            shared.Owners++;
        }

        if (joined is not null)
        {
            this.LogRunJoined(request, DescribeRun(joined));
            this.PublishRun(joined, reveal: !joined.Request.IsAutomatic);
        }

        var observation = this.ObserveSharedAsync(shared, cancellationToken);
        if (start)
        {
            _ = this.ExecuteSharedAsync(shared);
        }

        return observation;
    }

    // Called under stateLock. The same scope is preferred over a covering one.
    private SharedCook? FindPendingShared(SharedCookKey key, CookRunRequest request)
    {
        SharedCook? covering = null;
        foreach (var pending in this.pendingShared.Values)
        {
            if (pending.Key.Lifetime != key.Lifetime || pending.Key.ResultType != key.ResultType || !ReferenceEquals(pending.Key.Origin, key.Origin)
                || pending.Cancellation.IsCancellationRequested
                || (this.runs.TryGetValue(pending.OperationId, out var run) && run.Cancellation.IsCancellationRequested))
            {
                continue;
            }

            if (pending.Key == key)
            {
                return pending;
            }

            if (covering is null && Covers(pending.Request, request))
            {
                covering = pending;
            }
        }

        return covering;
    }

    private CookRunSnapshot? JoinShared(SharedCook shared, CookRunRequest incoming)
    {
        var promoted = shared.Request with
        {
            IsAutomatic = shared.Request.IsAutomatic && incoming.IsAutomatic,
            IsDemand = shared.Request.IsDemand || incoming.IsDemand,
            IsReimport = shared.Request.IsReimport || incoming.IsReimport,
        };
        shared.Request = promoted;
        if (!this.runs.TryGetValue(shared.OperationId, out var run))
        {
            return null;
        }

        this.revealQueueOutcome |= !promoted.IsAutomatic;
        var message = incoming.IsDemand ? "Active preview also needs this queued cook."
            : incoming.IsAutomatic ? $"Cook on change of {DescribeScope(incoming)} joined this queued cook."
            : $"Cook of {DescribeScope(incoming)} joined this queued cook.";
        run.Snapshot = Append(run.Snapshot with { Request = promoted }, new(Message: message));
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
    private async Task ExecuteSharedAsync<T>(SharedCook<T> shared)
    {
        try
        {
            // The work is read when the writer is granted: a foreground request may have joined the pending cook.
            var result = await this.RunCoreAsync((operation, token) => shared.Work(operation, token), shared.Request, shared.Cancellation.Token, shared).ConfigureAwait(false);
            _ = shared.Completion.TrySetResult(result);
        }
        catch (CookPreemptedException exception)
        {
            _ = shared.Completion.TrySetException(exception);
            _ = shared.Completion.Task.Exception;
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

    private sealed class SharedCook<T>(SharedCookKey key, CookRunRequest request, Func<ContentCookOperation, CancellationToken, Task<T>> work) : SharedCook(key, request)
    {
        public Func<ContentCookOperation, CancellationToken, Task<T>> Work { get; set; } = work;

        public TaskCompletionSource<T> Completion { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
