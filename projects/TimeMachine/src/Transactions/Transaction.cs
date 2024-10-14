// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Transactions;

using DroidNet.TimeMachine.Changes;

/// <summary>
/// Provides a simplified way to start and end multiple related changes via a "using" block. When the Transaction is disposed (at
/// the end of the using block) it will commit the changes to the relevant Undo or Redo stack.
/// </summary>
/// <remarks>
/// Nested transactions are supported.
/// </remarks>
public class Transaction : ITransaction
{
    private readonly ITransactionManager? owner;
    private readonly ChangeSet changes;

    private bool disposed;

    internal Transaction(ITransactionManager owner, object key)
    {
        this.changes = new ChangeSet { Key = key };
        this.owner = owner;
        this.owner.BeginTransaction(key);
    }

    public IEnumerable<IChange> Changes => this.changes.Changes;

    public object Key => this.changes.Key;

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    public void Commit() => this.owner?.CommitTransaction(this);

    public void Rollback() => this.owner?.RollbackTransaction(this);

    public void AddChange(IChange change) => this.changes.Add(change);

    public void Apply() => this.changes.Apply();

    private void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.owner?.CommitTransaction(this);
        }

        this.disposed = true;
    }
}
