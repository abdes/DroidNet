// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

public interface IStorageProvider
{
    /// <summary>
    /// Normalize the specified <paramref name="path" /> by transforming it into an absolute path if it's relative, removing
    /// redundant segments and trimming any trailing path separators from the end. It uses the current location to fully qualify
    /// the location specified in <paramref name="path" /> if it's relative.
    /// </summary>
    /// <param name="path">The path to be normalized.</param>
    /// <returns>
    /// The fully qualified absolute location path, with no trailing path separator.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If <paramref name="path" /> is not valid.
    /// </exception>
    string Normalize(string path);

    /// <summary>
    /// Combine two strings into a path and normalize it, where the first part (<paramref name="basePath" />) refers to the base
    /// path, to which the <paramref name="relativePath" /> will be appended.
    /// </summary>
    /// <param name="basePath">
    /// The base path.
    /// </param>
    /// <param name="relativePath">
    /// A relative path to the <paramref name="basePath" />. If this part, is rooted (i.e. starts with a root segment such as a
    /// path separator or a drive identifier), this method fails and throws <see cref="InvalidPathException" />.
    /// </param>
    /// <returns>
    /// The combined and normalized path.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If any of the parts contains an invalid path or if the <paramref name="relativePath" /> part is rooted.
    /// </exception>
    /// <seealso cref="Normalize" />
    string NormalizeRelativeTo(string basePath, string relativePath);

    /// <summary>
    /// Get a <see cref="IFolder" /> storage item corresponding to the given <paramref name="folderPath" />. The actual folder may
    /// not exist. Use <see cref="IStorageItem.ExistsAsync" /> to check it.
    /// </summary>
    /// <param name="folderPath">
    /// The path (relative or absolute) of the folder. This path is transformed into an absolute path and normalized before being
    /// used and becoming accessible via the <see cref="IStorageItem.Location">Location</see> property of the returned folder.
    /// </param>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method is successful, it will return a <see cref="IFolder" /> object, for which the underlying folder may or may
    /// not exist. If it does not exist, it may be created using <see cref="IFolder.CreateAsync" />.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If the specified path in <paramref name="folderPath" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// If an error related to the underlying storage system occurs. If this exception is thrown as a direct result of a previous
    /// exception, it will include a reference to the previous exception in the <see cref="Exception.InnerException">InnerException</see> property.
    /// </exception>
    Task<IFolder> GetFolderFromPathAsync(
        string folderPath,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Get a <see cref="IDocument" /> storage item corresponding to the given <paramref name="documentPath" />. The actual
    /// document may not exist. Use <see cref="IStorageItem.ExistsAsync" /> to check it.
    /// </summary>
    /// <param name="documentPath">
    /// The path (relative or absolute) of the document. This path is transformed into an absolute path and normalized before
    /// being used and becoming accessible via the <see cref="IStorageItem.Location">Location</see> property of the returned <see cref="IDocument" />.
    /// </param>
    /// <param name="cancellationToken">
    /// A cancellation token to observe while waiting for the task to complete.
    /// </param>
    /// <returns>
    /// When this method is successful, it will return a <see cref="IDocument" /> object, for which the underlying document may or
    /// may not exist. If it does not exist, it may be created using other methods available in the <see cref="IDocument" />
    /// class.
    /// </returns>
    /// <exception cref="InvalidPathException">
    /// If the specified path in <paramref name="documentPath" /> is not valid.
    /// </exception>
    /// <exception cref="StorageException">
    /// If an error related to the underlying storage system occurs. If this exception is thrown as a direct result of a previous
    /// exception, it will include a reference to the previous exception in the <see cref="Exception.InnerException">InnerException</see> property.
    /// </exception>
    Task<IDocument> GetDocumentFromPathAsync(
        string documentPath,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Checks if the specified path corresponds to an existing document.
    /// </summary>
    /// <param name="path">The path to the document.</param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation. The result is <see langword="true" /> if the caller has the
    /// required permissions and this item refers to an existing document; otherwise, <see langword="false" />.
    /// </returns>
    Task<bool> DocumentExistsAsync(string path);

    /// <summary>
    /// Checks if the specified path corresponds to an existing folder.
    /// </summary>
    /// <param name="path">The path to the folder.</param>
    /// <returns>
    /// A <see cref="Task" /> representing the asynchronous operation. The result is <see langword="true" /> if the caller has the
    /// required permissions and this item refers to an existing folder; otherwise, <see langword="false" />.
    /// </returns>
    Task<bool> FolderExistsAsync(string path);
}
