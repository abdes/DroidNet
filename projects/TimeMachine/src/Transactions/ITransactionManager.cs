// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Transactions;

/// <summary>
/// Represents a manager for handling transactions within the undo/redo system.
/// </summary>
/// <remarks>
/// The <see cref="ITransactionManager"/> interface defines the contract for managing transactions. Implementing this
/// interface allows for starting, committing, and rolling back transactions, ensuring that changes can be grouped
/// and managed effectively within the undo/redo system.
/// </remarks>
public interface ITransactionManager
{
    /// <summary>
    /// Begins a new transaction with the specified key.
    /// </summary>
    /// <param name="key">The key associated with the transaction.</param>
    /// <returns>A new transaction.</returns>
    /// <remarks>
    /// This method starts a new transaction and returns an instance of <see cref="ITransaction"/>. The key parameter
    /// is used to associate the transaction with a specific context or identifier.
    /// </remarks>
    public ITransaction BeginTransaction(object key);

    /// <summary>
    /// Commits the provided transaction, finalizing all the changes within it.
    /// </summary>
    /// <param name="transaction">The transaction to commit.</param>
    /// <exception cref="InvalidOperationException">The transaction is not in a state that allows committing.</exception>
    /// <remarks>
    /// This method finalizes the transaction, applying all the changes it contains. If the transaction is not in a
    /// state that allows committing, an <see cref="InvalidOperationException"/> is thrown.
    /// </remarks>
    public void CommitTransaction(ITransaction transaction);

    /// <summary>
    /// Rolls back the provided transaction, undoing all the changes made within it.
    /// </summary>
    /// <param name="transaction">The transaction to roll back.</param>
    /// <exception cref="InvalidOperationException">The transaction is not in a state that allows rolling back.</exception>
    /// <remarks>
    /// This method undoes all the changes made within the transaction. If the transaction is not in a state that
    /// allows rolling back, an <see cref="InvalidOperationException"/> is thrown.
    /// </remarks>
    public void RollbackTransaction(ITransaction transaction);
}
