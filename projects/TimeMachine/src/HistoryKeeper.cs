// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;

[SuppressMessage(
    "ReSharper",
    "ClassWithVirtualMembersNeverInherited.Global",
    Justification = "needed for mocking in unit tests")]
public class HistoryKeeper(object root, ITransactionFactory? transactionFactory = null) : ITransactionManager
{
    private readonly Stack<IChange> undoStack = new();
    private readonly Stack<IChange> redoStack = new();
    private readonly Stack<ITransaction> transactions = new();

    private readonly WeakReference root = new(root);

    public event EventHandler? UndoStackChanged;

    public event EventHandler? RedoStackChanged;

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
        /// Batched changes within a transcation are being committed.
        /// </summary>
        Committing = 3,

        /// <summary>
        /// Batched changes within a transcation are being rolled back.
        /// </summary>
        RollingBack = 4,
    }

    /// <summary>
    /// Gets a collection of undoable changes for the current Root.
    /// </summary>
    public IEnumerable<IChange> UndoStack => this.undoStack;

    /// <summary>
    /// Gets a collection of redoable changes for the current Root.
    /// </summary>
    public IEnumerable<IChange> RedoStack => this.redoStack;

    public bool CanUndo => this.undoStack.Count > 0;

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

    [SuppressMessage(
        "ReSharper",
        "MemberCanBePrivate.Global",
        Justification = "Must be accessible to the StateTransition class")]
    internal States State { get; set; } = States.Idle;

    public virtual void AddChange(IChange change)
    {
        if (this.State == States.Undoing)
        {
            this.redoStack.Push(change);
            this.OnRedoStackChanged();
            return;
        }

        if (this.State != States.Redoing)
        {
            this.redoStack.Clear();
            this.OnRedoStackChanged();
        }

        this.undoStack.Push(change);
        this.OnUndoStackChanged();
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

        if (!this.undoStack.TryPop(out var lastChange))
        {
            return;
        }

        using (new StateTransition<States>(this, States.Undoing))
        {
            lastChange.Apply();
            this.OnUndoStackChanged();
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

        if (!this.redoStack.TryPop(out var lastChange))
        {
            return;
        }

        using (new StateTransition<States>(this, States.Redoing))
        {
            lastChange.Apply();
            this.OnRedoStackChanged();
        }
    }

    public void Clear()
    {
        if (this.State != States.Idle)
        {
            throw new InvalidOperationException("unable to clear the undo history because we're not Idle");
        }

        this.undoStack.Clear();
        this.OnUndoStackChanged();

        this.redoStack.Clear();
        this.OnRedoStackChanged();
    }

    public ITransaction BeginTransaction(object key)
    {
        var transaction = transactionFactory is null
            ? new Transaction(this, key)
            : transactionFactory.CreateTransaction(key);

        this.transactions.Push(transaction);

        return transaction;
    }

    public void CommitTransaction(ITransaction transaction)
    {
        // Only switch the state to Committing if we are not doing another task (e.g. undoing).
        var currentState = this.State == States.Idle ? States.Committing : this.State;

        using (new StateTransition<States>(this, currentState))
        {
            while (this.transactions.TryPop(out var toCommit))
            {
                if (toCommit.Equals(transaction))
                {
                    if (this.State == States.Undoing)
                    {
                        this.redoStack.Push(transaction);
                        this.OnRedoStackChanged();
                    }
                    else
                    {
                        this.undoStack.Push(transaction);
                        this.OnUndoStackChanged();
                    }

                    break;
                }

                if (this.transactions.Count != 0)
                {
                    this.transactions.Peek().AddChange(toCommit);
                }
            }
        }
    }

    public void RollbackTransaction(ITransaction transaction)
    {
        // Only switch the state to RollingBack if we are not doing another task (e.g. undoing).
        var currentState = this.State == States.Idle ? States.RollingBack : this.State;

        using (new StateTransition<States>(this, currentState))
        {
            while (this.transactions.TryPop(out var toRollback) && toRollback != transaction)
            {
                toRollback.Rollback();
            }
        }
    }

    private void CommitTransactions() => this.transactions.FirstOrDefault()?.Commit();

    private void OnUndoStackChanged() => this.UndoStackChanged?.Invoke(this, EventArgs.Empty);

    private void OnRedoStackChanged() => this.RedoStackChanged?.Invoke(this, EventArgs.Empty);
}
