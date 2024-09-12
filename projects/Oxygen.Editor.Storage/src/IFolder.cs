// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

/// <summary>Represents a folder, which may contain other nested files and folders.</summary>
public interface IFolder : IStorageItem
{
    /// <summary>
    /// Gets a <see cref="IDocument" /> representing a document with the specified name under the current folder. The
    /// actual document may not exist. Use <see cref="IStorageItem.ExistsAsync" /> to check it.
    /// </summary>
    /// <param name="documentName">
    /// The name (or path relative to the current folder) of the document to get.
    /// </param>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method completes successfully, it returns a <see cref="IDocument" /> that represents the specified
    /// file.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="documentName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// An error was reported by the underlying storage subsystem.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    Task<IDocument> GetDocumentAsync(string documentName, CancellationToken cancellationToken = default);

    /// <summary>
    /// Gets the subfolder with the specified name from the current folder. The
    /// actual folder may not exist. Use <see cref="IStorageItem.ExistsAsync" /> to check it.
    /// </summary>
    /// <param name="folderName">
    /// The name (or path relative to the current folder) of the subfolder to get.
    /// </param>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method completes successfully, it returns a <see cref="IFolder" /> that represents the specified
    /// subfolder.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If the <paramref name="folderName" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// An error was reported by the underlying storage subsystem.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    Task<IFolder> GetFolderAsync(string folderName, CancellationToken cancellationToken = default);

    /// <summary>
    /// Asynchronously enumerate all storage items directly under the current folder.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method does not throw a <see cref="StorageException" />, it will return an enumerator that provides
    /// asynchronous iteration over the items under the current folder that could be successfully enumerated. Items
    /// which could not be successfully enumerated, due to any possible error, are ignored.
    /// </returns>
    /// <exception cref="StorageException">
    /// The enumeration of items under the folder completely failed.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    IAsyncEnumerable<IStorageItem> GetItemsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Asynchronously enumerate only documents directly under the current folder.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method does not throw a <see cref="StorageException" />, it will return an enumerator that provides
    /// asynchronous iteration over only documents under the current folder that could be successfully enumerated.
    /// Documents which could not be successfully enumerated, due to any possible error, are ignored.
    /// </returns>
    /// <exception cref="StorageException">
    /// The current folder does not exist, or the enumeration of documents completely failed.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    IAsyncEnumerable<IDocument> GetDocumentsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Asynchronously enumerate only folders directly under the current folder.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method does not throw a <see cref="StorageException" />, it will return an enumerator that provides
    /// asynchronous iteration over only folders under the current folder that could be successfully enumerated.
    /// Folders which could not be successfully enumerated, due to any possible error, are ignored.
    /// </returns>
    /// <exception cref="StorageException">
    /// The current folder does not exist, or the enumeration of documents completely failed.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    IAsyncEnumerable<IFolder> GetFoldersAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Create the underlying storage item corresponding to this folder if it does not exist. If the storage item
    /// already exists, this method does nothing.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation.
    /// </returns>
    /// <exception cref="StorageException">
    /// An error was reported by the underlying storage subsystem.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    Task CreateAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Delete the folder if it is empty. If the folder is not empty, this method will fail. Use <see cref="DeleteRecursiveAsync" />
    /// instead. If the folder does not exist, this method will succeed but does nothing.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation.
    /// </returns>
    /// <exception cref="StorageException">
    /// If the folder is not empty or if an error was reported by the underlying storage subsystem.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    Task DeleteAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Deletes the folder and any subfolders and documents in it. If the folder does not exist, this method will succeed but does
    /// nothing.
    /// </summary>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation.
    /// </returns>
    /// <exception cref="StorageException">
    /// If an error was reported by the underlying storage subsystem.
    /// The <see cref="Exception.InnerException">InnerException</see> property of the exception will indicate the root
    /// cause of the failure.
    /// </exception>
    Task DeleteRecursiveAsync(CancellationToken cancellationToken = default);
}
