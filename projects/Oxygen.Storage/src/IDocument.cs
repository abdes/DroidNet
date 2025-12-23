// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Storage;

/// <summary>
/// Represents a document item that is always nested within a folder.
/// Provides methods for deleting, copying, moving, and reading or writing the document asynchronously.
/// </summary>
public interface IDocument : INestedItem, IStorageItem
{
    /// <summary>
    /// Deletes the document. If the document does not exist, this method succeeds but performs no action.
    /// </summary>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests while the operation is running.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> that represents the asynchronous delete operation.
    /// </returns>
    /// <exception cref="StorageException">
    /// Thrown if an error occurs in the underlying storage system. The <see cref="Exception.InnerException" /> property may
    /// indicate the root cause (e.g., <see cref="UnauthorizedAccessException" />).
    /// </exception>
    public Task DeleteAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies the document to the specified destination folder. If the destination folder does not exist, it will be created
    /// along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to copy the document into.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{IDocument}" /> that represents the asynchronous copy operation, where the result is the newly created
    /// document in the destination folder.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// Thrown if an item with the same <see cref="IStorageItem.Name">Name</see> as the current document exists at the destination
    /// folder.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the copy operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task<IDocument> CopyAsync(IFolder destinationFolder, CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies the document to the specified destination folder with a new desired name. If the destination folder does not exist,
    /// it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to copy the document into.
    /// </param>
    /// <param name="desiredNewName">
    /// The new name for the copied document.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{IDocument}" /> that represents the asynchronous copy operation, where the result is the newly created
    /// document with the specified new name in the destination folder.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// Thrown if an item with the same <see cref="IStorageItem.Name">Name</see> as <paramref name="desiredNewName" />> exists at the destination
    /// folder.
    /// </exception>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="desiredNewName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the copy operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task<IDocument> CopyAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies the document to the specified destination folder, overwriting the destination if it exists. If the destination
    /// folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to copy the document into.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{IDocument}" /> that represents the asynchronous copy operation, where the result is the newly created or
    /// overwritten document in the destination folder.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the copy operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task<IDocument> CopyOverwriteAsync(IFolder destinationFolder, CancellationToken cancellationToken = default);

    /// <summary>
    /// Copies the document to the specified destination folder with a new desired name, overwriting the destination if it exists.
    /// If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to copy the document into.
    /// </param>
    /// <param name="desiredNewName">
    /// The new name for the copied document.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{IDocument}" /> that represents the asynchronous copy operation, where the result is the newly created
    /// document with the specified new name in the destination folder.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="desiredNewName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the copy operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task<IDocument> CopyOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Moves the document to the specified destination folder. If the destination folder does not exist, it will be created along
    /// with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to move the document into.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> that represents the asynchronous move operation.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// Thrown if an item with the same <see cref="IStorageItem.Name">Name</see> as the current document exists at the destination
    /// folder.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the move operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="UnauthorizedAccessException" />).
    /// </exception>
    public Task MoveAsync(IFolder destinationFolder, CancellationToken cancellationToken = default);

    /// <summary>
    /// Moves the document to the specified destination folder, overwriting the destination if it exists. If the destination
    /// folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to move the document into.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> that represents the asynchronous move operation.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the move operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task MoveOverwriteAsync(IFolder destinationFolder, CancellationToken cancellationToken = default);

    /// <summary>
    /// Moves the document to the specified destination folder with a new desired name. If the destination folder does not exist,
    /// it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to move the document into.
    /// </param>
    /// <param name="desiredNewName">
    /// The new name for the moved document.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> that represents the asynchronous move operation.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// Thrown if an item with the same <see cref="IStorageItem.Name">Name</see> as <paramref name="desiredNewName" />> exists at the destination
    /// folder.
    /// </exception>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="desiredNewName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the move operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task MoveAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Moves the document to the specified destination folder with a new desired name, overwriting the destination if it exists.
    /// If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">
    /// The folder to move the document into.
    /// </param>
    /// <param name="desiredNewName">
    /// The new name for the moved document.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> that represents the asynchronous move operation.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the source document does not exist.
    /// </exception>
    /// <exception cref="TargetExistsException">
    /// Thrown if an item with the same <see cref="IStorageItem.Name">Name</see> as <paramref name="desiredNewName" />> exists at
    /// the destination folder.
    /// </exception>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="desiredNewName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the move operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task MoveOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Reads the entire contents of the document as a string asynchronously, using the "UTF-8" encoding.
    /// </summary>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{String}" /> that represents the asynchronous read operation, containing the entire contents of the
    /// document as a string.
    /// </returns>
    /// <exception cref="ItemNotFoundException">
    /// Thrown if the document does not exist at the time of reading.
    /// </exception>
    /// <exception cref="StorageException">
    /// Thrown for other errors during the read operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task<string> ReadAllTextAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Asynchronously writes the specified string to the document, using the "UTF-8" encoding. If the document already exists, it
    /// is truncated and overwritten. If it does not exist, it is created.
    /// </summary>
    /// <param name="text">
    /// The string, representing the content, to write to the document.
    /// </param>
    /// <param name="cancellationToken">
    /// A token to monitor for cancellation requests.
    /// </param>
    /// <returns>
    /// A <see cref="Task{String}" /> that represents the asynchronous operation.
    /// </returns>
    /// <exception cref="StorageException">
    /// Thrown for any errors during the write operation. The <see cref="Exception.InnerException" /> may provide more specific
    /// information (e.g., <see cref="IOException" />).
    /// </exception>
    public Task WriteAllTextAsync(string text, CancellationToken cancellationToken = default);
}
