// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Storage;

/// <summary>
/// Represents an item stored in a storage system (a disk, an archive, a remote location, etc).
/// </summary>
/// <remarks>
/// Typically, you get <see cref="IStorageItem" /> objects by using methods in <see cref="IStorageProvider" /> or
/// <see cref="IFolder" /> which can create them.
/// </remarks>
public interface IStorageItem
{
    /// <summary>
    /// Gets the name of this storage item.
    /// </summary>
    /// <remarks>
    /// When the name should be part of the item's <see cref="Location" /> (such as for most of file-based storage systems), it
    /// must be valid as per the path validity requirements of the storage system.
    /// </remarks>
    /// <seealso cref="InvalidPathException" />
    public string Name { get; }

    /// <summary>
    /// Gets the full path where the item is located.
    /// </summary>
    /// <seealso cref="InvalidPathException" />
    public string Location { get; }

    /// <summary>
    /// Gets the last access date and time of this storage item. If the item does not exist or the caller does not have the
    /// required permission, a value corresponding to the earliest file time is returned (see <see cref="DateTime.FromFileTime" />).
    /// </summary>
    public DateTime LastAccessTime { get; }

    /// <summary>
    /// Checks if this storage item physically exists.
    /// </summary>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation. The result is <see langword="true" /> if the caller has the
    /// required permissions and this item refers to an existing physical storage item; otherwise, <see langword="false" />.
    /// </returns>
    public Task<bool> ExistsAsync();

    /// <summary>
    /// Renames this storage item.
    /// </summary>
    /// <param name="desiredNewName">The desired new name, which must be valid as required by the underlying storage system (see
    /// <see cref="InvalidPathException" />), should not contain path separators and should not refer to an existing folder or
    /// document under the current folder.</param>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="desiredNewName" /> is not valid.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// If a folder or document already exists under the parent of this folder, with the same name as <paramref name="desiredNewName" />.
    /// </exception>
    public Task RenameAsync(string desiredNewName, CancellationToken cancellationToken = default);
}
