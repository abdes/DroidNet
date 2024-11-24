// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;

namespace DroidNet.TimeMachine;

/// <summary>
/// Manages the history of changes for undo/redo operations.
/// </summary>
/// <remarks>
/// The <see cref="HistoryKeeper"/> class tracks changes and manages undo/redo operations for a specific root object.
/// It supports nested transactions and change sets, allowing for complex scenarios where multiple changes need to be
/// grouped and managed as a single unit.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var rootObject = new object();
/// var historyKeeper = new HistoryKeeper(rootObject);
///
/// using (var transaction = historyKeeper.BeginTransaction("exampleTransaction"))
/// {
///     var change = new CustomChange { Key = "change1" };
///     transaction.AddChange(change);
///     transaction.Commit();
/// }
///
/// historyKeeper.Undo();
/// historyKeeper.Redo();
/// ]]></code>
/// </example>
[SuppressMessage("ReSharper", "ClassWithVirtualMembersNeverInherited.Global", Justification = "needed for mocking in unit tests")]
public class HistoryKeeper : ITransactionManager
{
    private readonly ObservableCollection<IChange> redoStack = [];
    private readonly WeakReference root;

    private readonly ITransactionFactory? transactionFactory;
    private readonly Stack<ITransaction> transactions = new();
    private readonly ObservableCollection<IChange> undoStack = [];
    private ChangeSet? currentChangeSet;

    private int isCollectingChangesCounter;

    /// <summary>
    /// Initializes a new instance of the <see cref="HistoryKeeper"/> class with the specified root object and optional transaction factory.
    /// </summary>
    /// <param name="root">The root object for which changes are tracked.</param>
    /// <param name="transactionFactory">An optional factory for creating transactions.</param>
    public HistoryKeeper(object root, ITransactionFactory? transactionFactory = null)
    {
        this.transactionFactory = transactionFactory;
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

        /// <summary>
        /// Batched changes within a transaction are being committed.
        /// </summary>
        Committing = 3,

        /// <summary>
        /// Batched changes within a transaction are being rolled back.
        /// </summary>
        RollingBack = 4,
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
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "Must be accessible to the StateTransition class")]
    internal States State { get; set; } = States.Idle;

    /// <inheritdoc />
    public ITransaction BeginTransaction(object key)
    {
        var transaction = this.transactionFactory is null
            ? new Transaction(this, key)
            : this.transactionFactory.CreateTransaction(key);

        this.transactions.Push(transaction);

        return transaction;
    }

    /// <inheritdoc />
    public void CommitTransaction(ITransaction transaction)
    {
        // Only switch the state to Committing if we are not doing another task (e.g. undoing).
        var currentState = this.State == States.Idle ? States.Committing : this.State;

        using (new StateTransition<States>(this, currentState))
        {
            while (this.transactions.TryPop(out var toCommit))
            {
                try
                {
                    if (toCommit.Equals(transaction))
                    {
                        if (this.State == States.Undoing)
                        {
                            this.PushToRedoStack(transaction);
                        }
                        else
                        {
                            this.PushToUndoStack(transaction);
                        }

                        break;
                    }

                    if (this.transactions.Count != 0)
                    {
                        this.transactions.Peek().AddChange(toCommit);
                    }

                    toCommit = null;
                }
                finally
                {
                    toCommit?.Dispose();
                }
            }
        }
    }

    /// <inheritdoc />
    public void RollbackTransaction(ITransaction transaction)
    {
        // Only switch the state to RollingBack if we are not doing another task (e.g. undoing).
        var currentState = this.State == States.Idle ? States.RollingBack : this.State;

        using (new StateTransition<States>(this, currentState))
        {
#pragma warning disable CA2000 // toRollback.Rollback() is already a Dispose method
            while (this.transactions.TryPop(out var toRollback) && toRollback != transaction)
#pragma warning restore CA2000
            {
                toRollback.Rollback();
            }
        }
    }

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
    public void Undo()
    {
        if (this.transactions.Count != 0)
        {
            this.CommitTransactions();
        }

        if (!this.TryPopUndoStack(out var lastChange))
        {
            return;
        }

        using (new StateTransition<States>(this, States.Undoing))
        using (new ChangeSetUndoRedo(this, lastChange))
        {
            lastChange.Apply();
        }
    }

    /// <summary>
    /// Redo the first available change.
    /// </summary>
    public void Redo()
    {
        if (this.transactions.Count != 0)
        {
            this.CommitTransactions();
        }

        if (!this.TryPopRedoStack(out var lastChange))
        {
            return;
        }

        using (new StateTransition<States>(this, States.Redoing))
        using (new ChangeSetUndoRedo(this, lastChange))
        {
            lastChange.Apply();
        }
    }

    /// <summary>
    /// Clears the undo and redo stacks.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown if the <see cref="HistoryKeeper"/> is not in the <see cref="States.Idle"/> state.</exception>
    public void Clear()
    {
        if (this.State != States.Idle)
        {
            throw new InvalidOperationException("unable to clear the undo history because we're not Idle");
        }

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

        // ReSharper disable once SwitchStatementHandlesSomeKnownEnumValuesWithDefault
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

    private void ClearUndoStack() => this.undoStack.Clear();

    private bool TryPopRedoStack(out IChange value)
    {
        if (this.redoStack.Count > 0)
        {
            value = this.redoStack[0];
            this.redoStack.RemoveAt(0);
            return true;
        }

        value = default!;
        return false;
    }

    private bool TryPopUndoStack(out IChange value)
    {
        if (this.undoStack.Count > 0)
        {
            value = this.undoStack[^1];
            this.undoStack.RemoveAt(this.undoStack.Count - 1);
            return true;
        }

        value = default!;
        return false;
    }

    private void PushToRedoStack(IChange change) => this.redoStack.Insert(0, change);

    private void PushToUndoStack(IChange change) => this.undoStack.Add(change);

    private void ClearRedoStack() => this.redoStack.Clear();

    private void CommitTransactions() => this.transactions.FirstOrDefault()?.Commit();

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
