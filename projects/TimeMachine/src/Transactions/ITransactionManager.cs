// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Transactions;

public interface ITransactionManager
{
    /// <summary>
    /// Begins a new transaction with the specified key.
    /// </summary>
    /// <param name="key">The key associated with the transaction.</param>
    /// <returns>A new transaction.</returns>
    ITransaction BeginTransaction(object key);

    /// <summary>
    /// Commits the provided transaction, finalizing all the changes within it.
    /// </summary>
    /// <param name="transaction">The transaction to commit.</param>
    /// <exception cref="InvalidOperationException">The transaction is not in a state that allows committing.</exception>
    void CommitTransaction(ITransaction transaction);

    /// <summary>
    /// Rolls back the provided transaction, undoing all the changes made within it.
    /// </summary>
    /// <param name="transaction">The transaction to roll back.</param>
    /// <exception cref="InvalidOperationException">The transaction is not in a state that allows rolling back.</exception>
    void RollbackTransaction(ITransaction transaction);
}
