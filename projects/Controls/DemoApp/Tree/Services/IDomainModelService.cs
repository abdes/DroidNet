// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree.Services;

/// <summary>
/// Provides methods to mutate the domain model for tree operations (add, remove, move, rename).
/// <para>
/// The ViewModel delegates all direct model mutations to implementations of this interface
/// to keep model logic decoupled from UI concerns and to enable easier unit testing.
/// </para>
/// </summary>
internal interface IDomainModelService
{
    /// <summary>
    /// Attempts to insert <paramref name="item"/> into <paramref name="parent"/> at the
    /// requested <paramref name="index"/>.
    /// </summary>
    /// <param name="item">The adapter representing the item to insert.</param>
    /// <param name="parent">The adapter representing the parent to insert into.</param>
    /// <param name="index">The desired index to insert at.</param>
    /// <param name="errorMessage">If the operation fails, an explanatory error message; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the insertion succeeded; otherwise <see langword="false"/>.</returns>
    public bool TryInsert(ITreeItem item, ITreeItem parent, int index, out string? errorMessage);

    /// <summary>
    /// Attempts to remove <paramref name="item"/> from <paramref name="parent"/>.
    /// </summary>
    /// <param name="item">The adapter representing the item to remove.</param>
    /// <param name="parent">The adapter representing the parent the item was removed from.</param>
    /// <param name="errorMessage">If the operation fails, an explanatory error message; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the removal succeeded; otherwise <see langword="false"/>.</returns>
    public bool TryRemove(ITreeItem item, ITreeItem parent, out string? errorMessage);

    /// <summary>
    /// Attempts to apply the set of moves described by <paramref name="args"/> to the domain model.
    /// </summary>
    /// <param name="args">The batch move args containing move entries.</param>
    /// <param name="errorMessage">If the operation fails, an explanatory error message; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the updates succeeded; otherwise <see langword="false"/>.</returns>
    public bool TryUpdateMoved(TreeItemsMovedEventArgs args, out string? errorMessage);

    /// <summary>
    /// Attempts to rename the underlying domain model object for the supplied <paramref name="item"/>.
    /// </summary>
    /// <param name="item">The adapter representing the item to rename.</param>
    /// <param name="newName">The new name to assign.</param>
    /// <param name="errorMessage">If the operation fails, an explanatory error message; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the rename succeeded; otherwise <see langword="false"/>.</returns>
    public bool TryRename(ITreeItem item, string newName, out string? errorMessage);
}
