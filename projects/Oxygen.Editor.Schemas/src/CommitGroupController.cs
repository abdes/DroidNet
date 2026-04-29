// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Generic commit-group controller. Replaces per-VM session machinery
/// (active-sessions dictionary, edit-gate semaphore, wheel-idle
/// timers).
/// </summary>
/// <remarks>
/// <para>
/// A <see cref="CommitGroupController"/> represents one in-flight
/// gesture (a drag, a wheel scroll, a numeric-box edit). It captures the
/// "before" snapshot once at <see cref="Begin"/>, buffers preview
/// values, and produces a single <see cref="PropertyOp"/> at
/// <see cref="CommitAsync"/>. <see cref="Cancel"/> restores the
/// "before" snapshot directly.
/// </para>
/// <para>
/// Mouse-wheel idle commits are scheduled via
/// <see cref="ScheduleIdleCommitAsync"/>. The default delay is 250 ms;
/// callers can override per session.
/// </para>
/// </remarks>
public sealed class CommitGroupController
{
    /// <summary>
    /// The default mouse-wheel idle commit delay.
    /// </summary>
    public static readonly TimeSpan DefaultWheelIdleDelay = TimeSpan.FromMilliseconds(250);

    private readonly Dictionary<string, ActiveSession> active = new(StringComparer.Ordinal);
    private readonly object gate = new();

    /// <summary>
    /// Begins a session keyed by an arbitrary string (e.g. property
    /// pointer). If a session with the same key is already open, it is
    /// returned unchanged.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="nodes">The node set captured at session begin. Locked
    /// for the duration of the session — selection mutation must not
    /// alter this list.</param>
    /// <param name="before">The pre-edit snapshot. Captured once; used as
    /// the inverse operand at commit / cancel time.</param>
    /// <param name="label">A human-readable label for the history UI.</param>
    /// <returns>The active session handle.</returns>
    public CommitGroupSession Begin(string key, IReadOnlyList<Guid> nodes, PropertySnapshot before, string label)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(before);
        ArgumentException.ThrowIfNullOrEmpty(label);

        lock (this.gate)
        {
            if (this.active.TryGetValue(key, out var existing))
            {
                return existing.Handle;
            }

            var session = new ActiveSession
            {
                Handle = new CommitGroupSession(key, nodes, before, label),
                IdleCancellation = null,
            };
            this.active[key] = session;
            return session.Handle;
        }
    }

    /// <summary>
    /// Returns the active session for the given key, or <see langword="null"/>.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <returns>The session, or <see langword="null"/>.</returns>
    public CommitGroupSession? GetActive(string key)
    {
        lock (this.gate)
        {
            return this.active.TryGetValue(key, out var s) ? s.Handle : null;
        }
    }

    /// <summary>
    /// Records a buffered preview value for the active session.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="preview">The preview snapshot.</param>
    public void RecordPreview(string key, PropertySnapshot preview)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        ArgumentNullException.ThrowIfNull(preview);
        lock (this.gate)
        {
            if (this.active.TryGetValue(key, out var s))
            {
                s.Handle.LastPreview = preview;
            }
        }
    }

    /// <summary>
    /// Schedules a mouse-wheel idle commit. If another preview arrives
    /// before the delay elapses, callers should cancel by calling
    /// <see cref="CancelPendingIdle"/> and re-scheduling.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="delay">The idle delay.</param>
    /// <param name="commitAction">The asynchronous commit callback.</param>
    /// <returns>A task that completes when the idle commit either fires
    /// or is cancelled.</returns>
    public async Task ScheduleIdleCommitAsync(string key, TimeSpan delay, Func<Task> commitAction)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        ArgumentNullException.ThrowIfNull(commitAction);

        var cts = new CancellationTokenSource();
        lock (this.gate)
        {
            if (!this.active.TryGetValue(key, out var s))
            {
                cts.Dispose();
                return;
            }

            s.IdleCancellation?.Cancel();
            s.IdleCancellation?.Dispose();
            s.IdleCancellation = cts;
        }

        try
        {
            await Task.Delay(delay, cts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            cts.Dispose();
            return;
        }

        lock (this.gate)
        {
            if (!this.active.TryGetValue(key, out var activeSession)
                || !ReferenceEquals(activeSession.IdleCancellation, cts))
            {
                cts.Dispose();
                return;
            }

            activeSession.IdleCancellation = null;
        }

        cts.Dispose();
        await commitAction().ConfigureAwait(false);
    }

    /// <summary>
    /// Cancels any pending idle-commit timer for the given session.
    /// </summary>
    /// <param name="key">The session key.</param>
    public void CancelPendingIdle(string key)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        lock (this.gate)
        {
            if (this.active.TryGetValue(key, out var s))
            {
                s.IdleCancellation?.Cancel();
                s.IdleCancellation?.Dispose();
                s.IdleCancellation = null;
            }
        }
    }

    /// <summary>
    /// Closes the session, returning the (Before, After) snapshots needed
    /// to build the <see cref="PropertyOp"/>.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="after">The "after" snapshot — typically the last
    /// preview snapshot.</param>
    /// <returns>The closed session, or <see langword="null"/> when no
    /// session is open.</returns>
    public CommitGroupSession? Close(string key, PropertySnapshot after)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        ArgumentNullException.ThrowIfNull(after);

        lock (this.gate)
        {
            if (!this.active.Remove(key, out var s))
            {
                return null;
            }

            s.IdleCancellation?.Cancel();
            s.IdleCancellation?.Dispose();
            s.IdleCancellation = null;
            s.Handle.After = after;
            return s.Handle;
        }
    }

    private sealed class ActiveSession
    {
        public required CommitGroupSession Handle { get; init; }

        public CancellationTokenSource? IdleCancellation { get; set; }
    }
}

/// <summary>
/// Handle to a single in-flight commit-group session.
/// </summary>
public sealed class CommitGroupSession
{
    /// <summary>
    /// Initializes a new session handle.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="nodes">The captured node set.</param>
    /// <param name="before">The pre-edit snapshot.</param>
    /// <param name="label">The history label.</param>
    public CommitGroupSession(string key, IReadOnlyList<Guid> nodes, PropertySnapshot before, string label)
    {
        this.Key = key;
        this.Nodes = nodes;
        this.Before = before;
        this.Label = label;
    }

    /// <summary>
    /// Gets the session key.
    /// </summary>
    public string Key { get; }

    /// <summary>
    /// Gets the captured node set.
    /// </summary>
    public IReadOnlyList<Guid> Nodes { get; }

    /// <summary>
    /// Gets the pre-edit snapshot.
    /// </summary>
    public PropertySnapshot Before { get; }

    /// <summary>
    /// Gets the history label.
    /// </summary>
    public string Label { get; }

    /// <summary>
    /// Gets or sets the most recent preview snapshot. The
    /// <see cref="CommitGroupController.Close"/> method assigns
    /// <see cref="After"/> from this value at commit time.
    /// </summary>
    public PropertySnapshot? LastPreview { get; set; }

    /// <summary>
    /// Gets the post-edit snapshot. Set by <see cref="CommitGroupController.Close"/>.
    /// </summary>
    public PropertySnapshot? After { get; internal set; }
}
