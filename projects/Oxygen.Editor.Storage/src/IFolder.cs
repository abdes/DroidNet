// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

/// <summary>Represents a folder, which may contain other nested files and folders.</summary>
public interface IFolder : IStorageItem
{
    /// <summary>Gets items in this folder.</summary>
    /// <param name="kind">The type of items to enumerate.</param>
    /// <param name="cancellationToken">
    /// A <see cref="CancellationToken" /> that cancels
    /// this action.
    /// </param>
    /// <returns>
    /// Returns an async operation represented by <see cref="IAsyncEnumerable{T}" /> of
    /// type <see cref="IStorageItem" /> of the items in the folder that satisfy the
    /// specified <see cref="ProjectItemKind" />.
    /// </returns>
    IAsyncEnumerable<IStorageItem> GetItemsAsync(
        ProjectItemKind kind = ProjectItemKind.All,
        CancellationToken cancellationToken = default);
}
