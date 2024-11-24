// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine.Transactions;

/// <summary>
/// Represents a transaction that groups multiple changes and can be committed or rolled back.
/// </summary>
/// <remarks>
/// The <see cref="ITransaction"/> interface extends the <see cref="IChange"/> and <see cref="IDisposable"/> interfaces,
/// allowing transactions to be managed within the undo/redo system. Implementing this interface enables grouping
/// multiple changes into a single transaction that can be committed or rolled back as a unit.
/// </remarks>
public interface ITransaction : IChange, IDisposable
{
    /// <summary>
    /// Commits the transaction, finalizing all the changes within it.
    /// </summary>
    /// <remarks>
    /// This method should be called to finalize the transaction and apply all the changes it contains.
    /// </remarks>
    public void Commit();

    /// <summary>
    /// Rolls back the transaction, undoing all the changes made within it.
    /// </summary>
    /// <remarks>
    /// This method should be called to undo all the changes made within the transaction.
    /// </remarks>
    public void Rollback();

    /// <summary>
    /// Adds a change to the transaction.
    /// </summary>
    /// <param name="change">The change to add to the transaction.</param>
    /// <remarks>
    /// This method allows changes to be added to the transaction, grouping them together for commit or rollback.
    /// </remarks>
    public void AddChange(IChange change);
}
