// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine;

/// <summary>
/// Manages the history of changes for undo/redo operations.
/// </summary>
/// <remarks>
/// The <see cref="HistoryKeeper"/> class tracks changes and manages undo/redo operations for a specific root object.
/// Change sets group several changes into one entry, which undoes and redoes as a single unit.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var rootObject = new object();
/// var historyKeeper = new HistoryKeeper(rootObject);
///
/// historyKeeper.BeginChangeSet("exampleChangeSet");
/// try
/// {
///     historyKeeper.AddChange(new CustomChange { Key = "change1" });
///     historyKeeper.AddChange(new CustomChange { Key = "change2" });
/// }
/// finally
/// {
///     historyKeeper.EndChangeSet();
/// }
///
/// historyKeeper.Undo();
/// historyKeeper.Redo();
/// ]]></code>
/// </example>
public class HistoryKeeper
{
    private readonly ObservableCollection<IChange> redoStack = [];
    private readonly WeakReference root;

    private readonly ObservableCollection<IChange> undoStack = [];

    // The position each undo entry leads to, and the position redoing each redo entry leads to, aligned with the stacks.
    private readonly List<long> undoPositions = [];
    private readonly List<long> redoPositions = [];
    private ChangeSet? currentChangeSet;

    private int isCollectingChangesCounter;
    private bool isReplayingTo;
    private long lastPosition;
    private long emptyPosition;

    // The position of the entry being undone or redone, which the inverse it records leads back to.
    private long replayPosition;

    /// <summary>
    /// Initializes a new instance of the <see cref="HistoryKeeper"/> class with the specified root object.
    /// </summary>
    /// <param name="root">The root object for which changes are tracked.</param>
    public HistoryKeeper(object root)
    {
        this.root = new WeakReference(root);

        this.RedoStack = new ReadOnlyObservableCollection<IChange>(this.redoStack);
        this.UndoStack = new ReadOnlyObservableCollection<IChange>(this.undoStack);
    }

    /// <summary>
    /// Represents the various states of the undo/redo <see cref="UndoRedo" />.
    /// </summary>
    internal enum States
    {
        /// <summary>
        /// The <see cref="UndoRedo" /> is idle.
        /// </summary>
        Idle = 0,

        /// <summary>
        /// An undo operation is in progress.
        /// </summary>
        Undoing = 1,

        /// <summary>
        /// A redo operation is in progress.
        /// </summary>
        Redoing = 2,
    }

    /// <summary>
    /// Gets a collection of undoable changes for the current Root.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    /// <summary>
    /// Gets a collection of redo-able changes for the current Root.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    /// <summary>
    /// Gets a value indicating whether there are changes that can be undone.
    /// </summary>
    public bool CanUndo => this.undoStack.Count > 0;

    /// <summary>
    /// Gets a value indicating whether there are changes that can be redone.
    /// </summary>
    public bool CanRedo => this.redoStack.Count != 0;

    /// <summary>Gets the position of the current state, which undoing and redoing an entry restore.</summary>
    /// <remarks>It changes before the stacks do, so an observer of <see cref="UndoStack"/> reads the new position.</remarks>
    public HistoryPosition Position => new(this.undoPositions.Count == 0 ? this.emptyPosition : this.undoPositions[^1]);

    /// <summary>Gets a value indicating whether replay or a change set is in progress.</summary>
    public bool IsBusy => this.State != States.Idle || this.isReplayingTo || this.isCollectingChangesCounter != 0;

    /// <summary>
    /// Gets the instance that represents the root (or document) for this set of changes.
    /// </summary>
    /// <remarks>
    /// This is needed so that a single instance of the application can track undo histories
    /// for multiple "root" or "document" instances at the same time. These histories should not
    /// overlap or show in the same undo history.
    /// </remarks>
    public object? Root => this.root is { IsAlive: true } ? this.root.Target : null;

    /// <summary>
    /// Gets or sets the current state of the <see cref="HistoryKeeper"/>.
    /// </summary>
    /// <remarks>
    /// The <see cref="State"/> property represents the current state of the <see cref="HistoryKeeper"/>. It is used to
    /// manage the state transitions during undo, redo, commit, and rollback operations.
    /// </remarks>
    internal States State { get; set; } = States.Idle;

    // An undo or redo is running, maybe awaiting its change; another one would interleave with it and restore the state
    // out of order.
    private bool IsReplaying => this.State is States.Undoing or States.Redoing || this.isReplayingTo;

    /// <summary>
    /// Adds a change to the undo stack or the current change set.
    /// </summary>
    /// <param name="change">The change to add.</param>
    public virtual void AddChange(IChange change)
    {
        // A new change, unrelated to the Undo/Redo stack unwinding should reset the Redo stack
        if (this.State is not (States.Undoing or States.Redoing))
        {
            this.ClearRedoStack();
        }

        if (this.currentChangeSet is not null)
        {
            // We are collecting all changes in the current change set
            this.currentChangeSet.Add(change);
            return;
        }

        if (this.State == States.Undoing)
        {
            this.PushToRedoStack(change);
        }
        else
        {
            this.PushToUndoStack(change);
        }
    }

    /// <summary>
    /// Undo the first available change.
    /// </summary>
    /// <returns>
    /// <see langword="true"/> when a change was undone; <see langword="false"/> when there was nothing to undo, when an
    /// undo or redo is still running, or when
    /// the change threw, in which case it stays on the undo stack at its position, unless it recorded part of its
    /// inverse before it threw.
    /// </returns>
    public bool Undo() => !this.IsReplaying && this.ReplayNext(States.Undoing);

    /// <summary>
    /// Undo the first available change asynchronously.
    /// </summary>
    /// <remarks>Nothing is undone while an undo or redo is still running.</remarks>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    public async ValueTask UndoAsync(CancellationToken cancellationToken = default)
    {
        if (!this.IsReplaying)
        {
            await this.ReplayNextAsync(States.Undoing, cancellationToken).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Redo the first available change.
    /// </summary>
    /// <returns>
    /// <see langword="true"/> when a change was redone; <see langword="false"/> when there was nothing to redo, when an
    /// undo or redo is still running, or when
    /// the change threw, in which case it stays on the redo stack at its position, unless it recorded part of its
    /// inverse before it threw.
    /// </returns>
    public bool Redo() => !this.IsReplaying && this.ReplayNext(States.Redoing);

    /// <summary>
    /// Redo the first available change asynchronously.
    /// </summary>
    /// <remarks>Nothing is redone while an undo or redo is still running.</remarks>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    public async ValueTask RedoAsync(CancellationToken cancellationToken = default)
    {
        if (!this.IsReplaying)
        {
            await this.ReplayNextAsync(States.Redoing, cancellationToken).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Undoes or redoes entries, as one operation, until the undo stack holds <paramref name="undoCount"/> entries: the
    /// state after the oldest <paramref name="undoCount"/> entries of the history.
    /// </summary>
    /// <remarks>
    /// <para>
    /// A count below the undo stack's undoes, one above it redoes, and an equal one does nothing. <c>0</c> undoes everything,
    /// and the entries of both stacks redo everything. Each entry replays as <see cref="UndoAsync"/> or
    /// <see cref="RedoAsync"/> replays it.
    /// </para>
    /// <para>
    /// The history is busy until the operation ends: <see cref="Undo"/>, <see cref="Redo"/>, <see cref="UndoAsync"/>,
    /// <see cref="RedoAsync"/> and another <see cref="ReplayToAsync"/> do nothing meanwhile, and <see cref="Clear"/>
    /// throws. Nothing runs while an undo or redo is still running.
    /// </para>
    /// <para>
    /// A change that throws stops the operation where it is: that change stays on its stack, as after a failed
    /// <see cref="UndoAsync"/> or <see cref="RedoAsync"/>, the entries replayed before it stay replayed, and the exception
    /// propagates. Cancellation is checked before each entry and leaves the entries replayed so far replayed.
    /// </para>
    /// </remarks>
    /// <param name="undoCount">The number of entries the undo stack holds when the operation ends.</param>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="undoCount"/> is negative, or more than the entries of both stacks. Nothing is replayed. The upper
    /// bound is checked only when the history is not busy, since the stacks are between states then.
    /// </exception>
    /// <exception cref="OperationCanceledException">The operation was canceled before an entry replayed.</exception>
    /// <exception cref="InvalidOperationException">
    /// An entry applied without recording exactly one inverse, so the operation cannot go on. The entry is no longer on either
    /// stack.
    /// </exception>
    public async ValueTask ReplayToAsync(int undoCount, CancellationToken cancellationToken = default)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(undoCount);

        // While an undo, a redo or a replay runs, the stacks are between states, so the upper bound means nothing yet.
        if (this.IsReplaying)
        {
            return;
        }

        ArgumentOutOfRangeException.ThrowIfGreaterThan(undoCount, this.undoStack.Count + this.redoStack.Count);
        this.isReplayingTo = true;
        try
        {
            while (this.undoStack.Count != undoCount)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var undoing = this.undoStack.Count > undoCount;
                var entry = undoing ? this.undoStack[^1] : this.redoStack[0];
                var inverse = undoing ? this.redoStack : this.undoStack;
                var inverseCount = inverse.Count;
                await this.ReplayNextAsync(undoing ? States.Undoing : States.Redoing, cancellationToken).ConfigureAwait(true);
                ThrowIfNoInverse(entry, undoing ? "undoing" : "redoing", inverse.Count - inverseCount);
            }
        }
        finally
        {
            this.isReplayingTo = false;
        }
    }

    /// <summary>Gets the position the history is at after its oldest <paramref name="undoCount"/> entries.</summary>
    /// <remarks>
    /// The positions are the ones <see cref="Position"/> reports when the history is at that count: <c>0</c> is the state
    /// before the first entry, a count up to the undo stack's is the state an undo to it restores, and a count beyond it is
    /// the state a redo to it restores.
    /// </remarks>
    /// <param name="undoCount">The number of oldest entries, from <c>0</c> to the entries of both stacks.</param>
    /// <returns>The position of that state.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="undoCount"/> is negative, or more than the entries of both stacks.
    /// </exception>
    public HistoryPosition PositionAfter(int undoCount)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(undoCount);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(undoCount, this.undoStack.Count + this.redoStack.Count);
        if (undoCount == 0)
        {
            return new(this.emptyPosition);
        }

        return new(undoCount <= this.undoPositions.Count
            ? this.undoPositions[undoCount - 1]
            : this.redoPositions[undoCount - this.undoPositions.Count - 1]);
    }

    /// <summary>
    /// Clears the undo and redo stacks.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown if the <see cref="HistoryKeeper"/> is not in the <see cref="States.Idle"/> state.</exception>
    public void Clear()
    {
        if (this.State != States.Idle || this.isReplayingTo)
        {
            throw new InvalidOperationException("unable to clear the undo history because we're not Idle");
        }

        this.emptyPosition = ++this.lastPosition;
        this.ClearUndoStack();
        this.ClearRedoStack();
    }

    /// <summary>
    /// Tells the <see cref="HistoryKeeper" /> that all subsequent changes should be part of a single <see cref="ChangeSet" />
    /// until the next call to <see cref="EndChangeSet" />.
    /// </summary>
    /// <param name="key">
    /// An object attached to the created <see cref="ChangeSet" /> to help identify it.
    /// </param>
    /// <exception cref="InvalidOperationException">
    /// If a <see cref="ChangeSet" /> is started while the <see cref="HistoryKeeper" /> is not in the <see cref="States.Idle" />
    /// state.
    /// </exception>
    [SuppressMessage("Style", "IDE0010:Add missing cases", Justification = "only valid states are Undoing and Redoing")]
    public void BeginChangeSet(object key)
    {
        this.isCollectingChangesCounter++;
        if (this.isCollectingChangesCounter != 1)
        {
            return;
        }

        this.currentChangeSet = new ChangeSet { Key = key };

        switch (this.State)
        {
            case States.Undoing:
                this.PushToRedoStack(this.currentChangeSet);
                break;

            case States.Redoing:
            case States.Idle:
                this.PushToUndoStack(this.currentChangeSet);
                break;

            default:
                throw new InvalidOperationException($"Invalid state {this.State} to start a new change set");
        }
    }

    /// <summary>
    /// Ends the current change set.
    /// </summary>
    public void EndChangeSet()
    {
        this.isCollectingChangesCounter--;

        if (this.isCollectingChangesCounter < 0)
        {
            this.isCollectingChangesCounter = 0;
        }

        if (this.isCollectingChangesCounter == 0)
        {
            this.currentChangeSet = null;
        }
    }

    private static bool RemoveEmptyReplay(ObservableCollection<IChange> inverse, List<long> positions, int previousCount, IChange? added)
    {
        if (inverse.Count == previousCount + 1 && added is ChangeSet set && !set.Changes.Any())
        {
            var index = inverse.IndexOf(set);
            positions.RemoveAt(index);
            inverse.RemoveAt(index);
        }

        return inverse.Count == previousCount;
    }

    // Every real change records its inverse when it applies; one that does not is a defect of that change, and the walk
    // cannot tell where it stands once the entry is gone.
    private static void ThrowIfNoInverse(IChange entry, string replay, int recorded)
    {
        if (recorded != 1)
        {
            throw new InvalidOperationException(
                string.Create(CultureInfo.InvariantCulture, $"Replaying stopped: {replay} the history entry '{entry.Key}' ({entry.GetType().Name}) recorded {recorded} ")
                + "inverse changes instead of one, so the entry is no longer on either stack.");
        }
    }

    // Undoes or redoes the next entry: nothing happens when its stack is empty, and a change that throws goes back to its
    // stack at its position, unless it recorded part of its inverse.
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A change that fails to apply leaves the history as it was, and the result tells the caller.")]
    private bool ReplayNext(States replay)
    {
        if (!this.TryBeginReplay(replay, out var change, out var position, out var inverseCount))
        {
            return false;
        }

        try
        {
            using (new StateTransition<States>(this, replay))
            using (new ChangeSetUndoRedo(this, change))
            {
                change.Apply();
            }

            return true;
        }
        catch (Exception)
        {
            this.RollBackReplay(replay, change, position, inverseCount);
            return false;
        }
    }

    private async ValueTask ReplayNextAsync(States replay, CancellationToken cancellationToken)
    {
        if (!this.TryBeginReplay(replay, out var change, out var position, out var inverseCount))
        {
            return;
        }

        try
        {
            using (new StateTransition<States>(this, replay))
            using (new ChangeSetUndoRedo(this, change))
            {
                // The stacks are observable, and their observers, such as a view's Undo button, live on the caller's
                // context; the history changes them only there, after a failed change as after a successful one.
                await change.ApplyAsync(cancellationToken).ConfigureAwait(true);
            }
        }
        catch
        {
            this.RollBackReplay(replay, change, position, inverseCount);
            throw;
        }
    }

    // Takes the entry to replay off its stack: the last undo entry, or the next redo entry.
    private bool TryBeginReplay(States replay, out IChange change, out long position, out int inverseCount)
    {
        var undoing = replay == States.Undoing;
        if (!(undoing ? this.TryPopUndoStack(out change, out position) : this.TryPopRedoStack(out change, out position)))
        {
            inverseCount = 0;
            return false;
        }

        this.replayPosition = position;
        inverseCount = undoing ? this.redoStack.Count : this.undoStack.Count;
        return true;
    }

    // Puts a change that threw back on its stack, unless it recorded part of its inverse on the other.
    private void RollBackReplay(States replay, IChange change, long position, int inverseCount)
    {
        if (replay == States.Undoing)
        {
            if (RemoveEmptyReplay(this.redoStack, this.redoPositions, inverseCount, this.redoStack.FirstOrDefault()))
            {
                this.PushToUndoStack(change, position);
            }
        }
        else if (RemoveEmptyReplay(this.undoStack, this.undoPositions, inverseCount, this.undoStack.LastOrDefault()))
        {
            this.PushToRedoStack(change, position);
        }
    }

    private void ClearUndoStack()
    {
        this.undoPositions.Clear();
        this.undoStack.Clear();
    }

    private bool TryPopRedoStack(out IChange value, out long position)
    {
        if (this.redoStack.Count > 0)
        {
            value = this.redoStack[0];
            position = this.redoPositions[0];
            this.redoPositions.RemoveAt(0);
            this.redoStack.RemoveAt(0);
            return true;
        }

        value = default!;
        position = default;
        return false;
    }

    private bool TryPopUndoStack(out IChange value, out long position)
    {
        if (this.undoStack.Count > 0)
        {
            value = this.undoStack[^1];
            position = this.undoPositions[^1];
            this.undoPositions.RemoveAt(this.undoPositions.Count - 1);
            this.undoStack.RemoveAt(this.undoStack.Count - 1);
            return true;
        }

        value = default!;
        position = default;
        return false;
    }

    // Undoing records the inverse of the entry it undid; redoing that inverse returns to where the entry led.
    private void PushToRedoStack(IChange change) => this.PushToRedoStack(change, this.replayPosition);

    private void PushToRedoStack(IChange change, long position)
    {
        this.redoPositions.Insert(0, position);
        this.redoStack.Insert(0, change);
    }

    // Redoing records the inverse of the entry it redid, which leads where that entry led; a change of presentation
    // stays where the history is; anything else is a new state.
    private void PushToUndoStack(IChange change)
        => this.PushToUndoStack(
            change,
            this.State == States.Redoing ? this.replayPosition : change.ChangesState ? ++this.lastPosition : this.Position.Value);

    private void PushToUndoStack(IChange change, long position)
    {
        this.undoPositions.Add(position);
        this.undoStack.Add(change);
    }

    private void ClearRedoStack()
    {
        this.redoPositions.Clear();
        this.redoStack.Clear();
    }

    private sealed class ChangeSetUndoRedo : IDisposable
    {
        private readonly HistoryKeeper? keeper;

        private bool disposed;

        public ChangeSetUndoRedo(HistoryKeeper keeper, IChange lastChange)
        {
            if (lastChange is not ChangeSet)
            {
                return;
            }

            this.keeper = keeper;
            keeper.BeginChangeSet(lastChange.Key);
        }

        public void Dispose() => this.Dispose(disposing: true);

        private void Dispose(bool disposing)
        {
            if (this.disposed)
            {
                return;
            }

            if (disposing && this.keeper is not null)
            {
                this.keeper.EndChangeSet();
            }

            this.disposed = true;
        }
    }
}
