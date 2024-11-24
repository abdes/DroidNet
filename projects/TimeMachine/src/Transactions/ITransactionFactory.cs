// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Transactions;

/// <summary>
/// Represents a factory for creating transactions.
/// </summary>
/// <remarks>
/// The <see cref="ITransactionFactory"/> interface defines the contract for a factory that creates instances of
/// <see cref="ITransaction"/>. Implementing this interface allows for custom transaction creation logic, which
/// can be useful for scenarios where transactions need to be created with specific configurations or dependencies.
/// </remarks>
public interface ITransactionFactory
{
    /// <summary>
    /// Creates a new transaction with the specified key.
    /// </summary>
    /// <param name="key">The key associated with the transaction.</param>
    /// <returns>A new instance of <see cref="ITransaction"/>.</returns>
    /// <remarks>
    /// This method is responsible for creating and returning a new transaction instance. The key parameter is used
    /// to associate the transaction with a specific context or identifier.
    /// </remarks>
    public ITransaction CreateTransaction(object key);
}
