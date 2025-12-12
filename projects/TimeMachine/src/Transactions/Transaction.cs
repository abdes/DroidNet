// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine.Transactions;

/// <summary>
/// Provides a simplified way to start and end multiple related changes via a "using" block. When
/// the <see cref="Transaction"/> is disposed (at the end of the using block), it will commit the changes to the
/// relevant Undo or Redo stack.
/// </summary>
/// <remarks>
/// Nested transactions are supported.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var transactionManager = new CustomTransactionManager();
/// using (var transaction = transactionManager.BeginTransaction("exampleTransaction"))
/// {
///     var change = new CustomChange { Key = "change1" };
///     transaction.AddChange(change);
///     // Other changes can be added here
///     transaction.Commit();
/// }
///
/// // Alternatively, the transaction can be rolled back
/// using (var transaction = transactionManager.BeginTransaction("exampleTransaction"))
/// {
///     var change = new CustomChange { Key = "change1" };
///     transaction.AddChange(change);
///     // Other changes can be added here
///     transaction.Rollback();
/// }
/// ]]></code>
/// </example>
public sealed class Transaction : ITransaction
{
    private readonly ChangeSet changes;
    private readonly ITransactionManager? owner;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="Transaction"/> class with the specified owner and key.
    /// </summary>
    /// <param name="owner">The transaction manager that owns this transaction.</param>
    /// <param name="key">The key associated with this transaction.</param>
    internal Transaction(ITransactionManager owner, object key)
    {
        this.changes = new ChangeSet { Key = key };
        this.owner = owner;
    }

    /// <summary>
    /// Gets the collection of changes in this transaction.
    /// </summary>
    /// <value>
    /// An enumerable collection of changes.
    /// </value>
    public IEnumerable<IChange> Changes => this.changes.Changes;

    /// <summary>
    /// Gets the key associated with this transaction.
    /// </summary>
    /// <value>
    /// An object that uniquely identifies the transaction.
    /// </value>
    public object Key => this.changes.Key;

    /// <summary>
    /// Disposes the transaction, committing the changes if not already disposed.
    /// </summary>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.owner?.CommitTransaction(this);
        this.isDisposed = true;
    }

    /// <summary>
    /// Commits the transaction, finalizing all the changes within it.
    /// </summary>
    public void Commit()
    {
        this.owner?.CommitTransaction(this);
        this.isDisposed = true;
    }

    /// <summary>
    /// Rolls back the transaction, undoing all the changes made within it.
    /// </summary>
    public void Rollback()
    {
        this.owner?.RollbackTransaction(this);
        this.isDisposed = true;
    }

    /// <summary>
    /// Adds a change to this transaction.
    /// </summary>
    /// <param name="change">The change to add.</param>
    public void AddChange(IChange change) => this.changes.Add(change);

    /// <summary>
    /// Applies all changes in this transaction.
    /// </summary>
    public void Apply() => this.changes.Apply();

    /// <summary>
    /// Applies all changes in this transaction asynchronously.
    /// </summary>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    public ValueTask ApplyAsync(CancellationToken cancellationToken = default) => this.changes.ApplyAsync(cancellationToken);

    /// <summary>
    /// Returns a string representation of the transaction.
    /// </summary>
    /// <returns>
    /// A string that represents the transaction, typically the string representation of the <see cref="Key"/>.
    /// </returns>
    public override string? ToString() => this.Key.ToString();
}
