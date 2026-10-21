// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
///     The only handler of Aura's window closing: it runs the participants of each close in their level's order, so the
///     documents ask about their unsaved changes before the window, its session and the editor do their part.
/// </summary>
/// <remarks>
///     <para>
///     Every participant prepares the close; one that refuses keeps the window open, and the steps already prepared are
///     released. Once every handler of the close approved, the steps commit; one that refuses keeps the window open too.
///     When every step committed, each finalizes, before the window is destroyed.
///     </para>
///     <para>
///     The participants of the editor scope come with the service; a participant of a narrower scope, such as a
///     window's documents, joins while it lives. Participants of the same level run in the order they joined.
///     </para>
/// </remarks>
public sealed partial class CloseAndExit : ICloseAndExit, IDisposable
{
    private readonly IWindowManagerService windows;
    private readonly ILogger logger;
    private readonly Lock gate = new();
    private readonly List<IWindowCloseParticipant> participants;

    /// <summary>Initializes a new instance of the <see cref="CloseAndExit" /> class.</summary>
    /// <param name="windows">The window manager, whose window closing the service handles.</param>
    /// <param name="participants">The participants of the editor scope.</param>
    /// <param name="loggerFactory">Optional factory of the service's logger.</param>
    public CloseAndExit(IWindowManagerService windows, IEnumerable<IWindowCloseParticipant> participants, ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(windows);
        ArgumentNullException.ThrowIfNull(participants);
        this.windows = windows;
        this.participants = [.. participants];
        this.logger = loggerFactory?.CreateLogger<CloseAndExit>() ?? NullLogger<CloseAndExit>.Instance;
        windows.WindowClosing += this.OnWindowClosingAsync;
    }

    /// <inheritdoc />
    public IDisposable Join(IWindowCloseParticipant participant)
    {
        ArgumentNullException.ThrowIfNull(participant);
        lock (this.gate)
        {
            this.participants.Add(participant);
        }

        return new Membership(this, participant);
    }

    /// <inheritdoc />
    public void Dispose() => this.windows.WindowClosing -= this.OnWindowClosingAsync;

    private static void Release(IEnumerable<PreparedStep> steps)
    {
        foreach (var prepared in steps)
        {
            prepared.Step.Dispose();
        }
    }

    private async Task OnWindowClosingAsync(object? sender, WindowClosingEventArgs args)
    {
        if (args.Cancel)
        {
            return;
        }

        var open = this.windows.OpenWindows;
        var close = new WindowClose
        {
            Window = args.WindowId,
            IsLastWindow = open.All(window => window.Id == args.WindowId),
        };
        this.LogClosing(close, open.Count);
        var steps = await this.PrepareAsync(close).ConfigureAwait(false);
        if (steps is null)
        {
            args.Cancel = true;
            return;
        }

        args.AddCompletionTask(approved => this.CommitAsync(args, close, steps, approved));
        args.AddFinalizationTask(() => this.FinalizeAsync(close, steps));
    }

    // The steps of every participant, in level order, or null when one keeps the window open.
    private async Task<List<PreparedStep>?> PrepareAsync(WindowClose close)
    {
        IWindowCloseParticipant[] ordered;
        lock (this.gate)
        {
            ordered = [.. this.participants.OrderBy(static participant => participant.Level)];
        }

        var steps = new List<PreparedStep>(ordered.Length);
        try
        {
            foreach (var participant in ordered)
            {
                if (await participant.PrepareAsync(close).ConfigureAwait(false) is not { } step)
                {
                    this.LogKeptOpen(close, participant.Level);
                    Release(steps);
                    return null;
                }

                this.LogPrepared(close, participant, step);
                steps.Add(new PreparedStep(participant.Level, step));
            }
        }
        catch
        {
            Release(steps);
            throw;
        }

        return steps;
    }

    private async Task CommitAsync(WindowClosingEventArgs args, WindowClose close, List<PreparedStep> steps, bool approved)
    {
        var committed = false;
        try
        {
            if (!approved)
            {
                this.LogNotApproved(close);
                return;
            }

            foreach (var prepared in steps)
            {
                if (!await prepared.Step.CommitAsync().ConfigureAwait(false))
                {
                    this.LogCommitRefused(close);
                    args.Cancel = true;
                    return;
                }
            }

            committed = true;
            this.LogCommitted(close);
        }
        finally
        {
            // The finalizers release the steps of a committed close.
            if (!committed)
            {
                Release(steps);
            }
        }
    }

    private async Task FinalizeAsync(WindowClose close, List<PreparedStep> steps)
    {
        try
        {
            foreach (var prepared in steps)
            {
                try
                {
                    await prepared.Step.FinalizeAsync().ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    this.LogFinalizeFailed(exception, close, prepared.Level);
                    throw;
                }
            }

            this.LogFinalized(close);
        }
        finally
        {
            Release(steps);
        }
    }

    private void Leave(IWindowCloseParticipant participant)
    {
        lock (this.gate)
        {
            _ = this.participants.Remove(participant);
        }
    }

    // A participant's prepared step, with the level it runs at.
    private readonly record struct PreparedStep(WindowCloseLevel Level, WindowCloseStep Step);

    private sealed class Membership(CloseAndExit owner, IWindowCloseParticipant participant) : IDisposable
    {
        private CloseAndExit? owner = owner;

        public void Dispose() => Interlocked.Exchange(ref this.owner, value: null)?.Leave(participant);
    }
}
