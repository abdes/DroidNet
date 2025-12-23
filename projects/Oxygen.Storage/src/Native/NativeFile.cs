// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Text;

namespace Oxygen.Storage.Native;

/// <summary>
/// Represents a document in the file system, providing methods for deleting, copying, moving,
/// reading, and writing the document.
/// </summary>
public class NativeFile : IDocument
{
    /// <summary>
    /// Initializes a new instance of the <see cref="NativeFile"/> class.
    /// </summary>
    /// <param name="storageProvider">The storage provider used to manage the file system operations.</param>
    /// <param name="name">The name of the document.</param>
    /// <param name="path">The full path of the document.</param>
    /// <param name="parentPath">The path of the parent folder containing the document.</param>
    internal NativeFile(
        NativeStorageProvider storageProvider,
        string name,
        string path,
        string parentPath)
    {
        this.StorageProvider = storageProvider;
        this.ParentPath = parentPath;
        this.Name = name;
        this.Location = path;
    }

    /// <summary>
    /// Gets the path of the parent folder containing the current item.
    /// </summary>
    public string ParentPath { get; private set; }

    /// <summary>
    /// Gets the name of this storage item.
    /// </summary>
    public string Name { get; private set; }

    /// <summary>
    /// Gets the full path where the item is located.
    /// </summary>
    public string Location { get; private set; }

    /// <summary>
    /// Gets the last access date and time of this storage item. If the item does not exist or the
    /// caller does not have the required permission, a value corresponding to the earliest file
    /// time is returned (see <see cref="DateTime.FromFileTime" />).
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "We do want to catch all exception and return the default value of 0")]
    public DateTime LastAccessTime
    {
        get
        {
            try
            {
                var fs = this.StorageProvider.FileSystem;
                return fs.File.GetLastAccessTime(this.Location);
            }
            catch (Exception)
            {
                return DateTime.FromFileTime(0);
            }
        }
    }

    private NativeStorageProvider StorageProvider { get; }

    /// <summary>
    /// Checks if this storage item physically exists.
    /// </summary>
    /// <returns>
    /// A <see cref="Task"/> representing the asynchronous operation. The result is <see langword="true"/>
    /// if the caller has the required permissions and this item refers to an existing physical storage
    /// item; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> ExistsAsync() => Task.FromResult(this.StorageProvider.FileSystem.File.Exists(this.Location));

    /// <summary>
    /// Deletes the document. If the document does not exist, this method succeeds but performs no action.
    /// </summary>
    /// <param name="cancellationToken">A token to monitor for cancellation requests while the operation is running.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous delete operation.</returns>
    /// <exception cref="StorageException">Thrown if an error occurs in the underlying storage system. The <see cref="Exception.InnerException"/> property may indicate the root cause (e.g., <see cref="UnauthorizedAccessException"/>).</exception>
    public async Task DeleteAsync(CancellationToken cancellationToken = default)
    {
        // Check if the file does not exist
        if (!await this.ExistsAsync().ConfigureAwait(true))
        {
            return;
        }

        // Delete the file
        try
        {
            var fs = this.StorageProvider.FileSystem;
            await Task.Run(() => fs.File.Delete(this.Location), cancellationToken).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not delete folder at location [{this.Location}]", ex);
        }
    }

    /// <summary>
    /// Copies the document to the specified destination folder. If the destination folder does not
    /// exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to copy the document into.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task{IDocument}"/> that represents the asynchronous copy operation, where the result is the newly created document in the destination folder.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="TargetExistsException">Thrown if an item with the same <see cref="IStorageItem.Name"/> as the current document exists at the destination folder.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the copy operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task<IDocument> CopyAsync(IFolder destinationFolder, CancellationToken cancellationToken = default)
        => this.CopyAsync(destinationFolder, this.Name, overwrite: false, cancellationToken);

    /// <summary>
    /// Copies the document to the specified destination folder with a new desired name. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to copy the document into.</param>
    /// <param name="desiredNewName">The new name for the copied document.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task{IDocument}"/> that represents the asynchronous copy operation, where the result is the newly created document with the specified new name in the destination folder.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="TargetExistsException">Thrown if an item with the same <see cref="IStorageItem.Name"/> as <paramref name="desiredNewName"/> exists at the destination folder.</exception>
    /// <exception cref="InvalidPathException">If the <paramref name="desiredNewName"/> is not valid.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the copy operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task<IDocument> CopyAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            desiredNewName,
            overwrite: false,
            cancellationToken);

    /// <summary>
    /// Copies the document to the specified destination folder, overwriting the destination if it exists. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to copy the document into.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task{IDocument}"/> that represents the asynchronous copy operation, where the result is the newly created or overwritten document in the destination folder.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the copy operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task<IDocument> CopyOverwriteAsync(
        IFolder destinationFolder,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            this.Name,
            overwrite: true,
            cancellationToken);

    /// <summary>
    /// Copies the document to the specified destination folder with a new desired name, overwriting the destination if it exists. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to copy the document into.</param>
    /// <param name="desiredNewName">The new name for the copied document.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task{IDocument}"/> that represents the asynchronous copy operation, where the result is the newly created document with the specified new name in the destination folder.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="InvalidPathException">If the <paramref name="desiredNewName"/> is not valid.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the copy operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task<IDocument> CopyOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            desiredNewName,
            overwrite: true,
            cancellationToken);

    /// <summary>
    /// Moves the document to the specified destination folder. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to move the document into.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous move operation.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="TargetExistsException">Thrown if an item with the same <see cref="IStorageItem.Name"/> as the current document exists at the destination folder.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the move operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="UnauthorizedAccessException"/>).</exception>
    public Task MoveAsync(IFolder destinationFolder, CancellationToken cancellationToken = default)
        => this.MoveAsync(destinationFolder, this.Name, overwrite: false, cancellationToken);

    /// <summary>
    /// Moves the document to the specified destination folder with a new desired name. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to move the document into.</param>
    /// <param name="desiredNewName">The new name for the moved document.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous move operation.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="TargetExistsException">Thrown if an item with the same <see cref="IStorageItem.Name"/> as <paramref name="desiredNewName"/> exists at the destination folder.</exception>
    /// <exception cref="InvalidPathException">If the <paramref name="desiredNewName"/> is not valid.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the move operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task MoveAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            desiredNewName,
            overwrite: false,
            cancellationToken);

    /// <summary>
    /// Moves the document to the specified destination folder, overwriting the destination if it exists. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to move the document into.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous move operation.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the move operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task MoveOverwriteAsync(
        IFolder destinationFolder,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            this.Name,
            overwrite: true,
            cancellationToken);

    /// <summary>
    /// Moves the document to the specified destination folder with a new desired name, overwriting the destination if it exists. If the destination folder does not exist, it will be created along with any missing parent folders.
    /// </summary>
    /// <param name="destinationFolder">The folder to move the document into.</param>
    /// <param name="desiredNewName">The new name for the moved document.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous move operation.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the source document does not exist.</exception>
    /// <exception cref="TargetExistsException">Thrown if an item with the same <see cref="IStorageItem.Name"/> as <paramref name="desiredNewName"/> exists at the destination folder.</exception>
    /// <exception cref="InvalidPathException">If the <paramref name="desiredNewName"/> is not valid.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the move operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public Task MoveOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            desiredNewName,
            overwrite: true,
            cancellationToken);

    /// <summary>
    /// Reads the entire contents of the document as a string asynchronously, using the "UTF-8" encoding.
    /// </summary>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task{String}"/> that represents the asynchronous read operation, containing the entire contents of the document as a string.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the document does not exist at the time of reading.</exception>
    /// <exception cref="StorageException">Thrown for other errors during the read operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public async Task<string> ReadAllTextAsync(CancellationToken cancellationToken = default)
    {
        var fs = this.StorageProvider.FileSystem;
        try
        {
            return await fs.File.ReadAllTextAsync(this.Location, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(true);
        }
        catch (FileNotFoundException ex)
        {
            throw new ItemNotFoundException(ex.Message);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not read content of document [{this.Location}]", ex);
        }
    }

    /// <summary>
    /// Asynchronously writes the specified string to the document, using the "UTF-8" encoding. If the document already exists, it is truncated and overwritten. If it does not exist, it is created.
    /// </summary>
    /// <param name="text">The string, representing the content, to write to the document.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="Task"/> that represents the asynchronous operation.</returns>
    /// <exception cref="StorageException">Thrown for any errors during the write operation. The <see cref="Exception.InnerException"/> may provide more specific information (e.g., <see cref="IOException"/>).</exception>
    public async Task WriteAllTextAsync(string text, CancellationToken cancellationToken = default)
    {
        var fs = this.StorageProvider.FileSystem;
        try
        {
            await fs.File.WriteAllTextAsync(this.Location, text, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not write text content to document [{this.Location}]", ex);
        }
    }

    /// <summary>
    /// Opens the document for read access.
    /// </summary>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A readable stream positioned at the beginning of the document.</returns>
    /// <exception cref="ItemNotFoundException">Thrown if the document does not exist at the time of opening.</exception>
    /// <exception cref="StorageException">Thrown for errors during the open operation.</exception>
    public async Task<Stream> OpenReadAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!await this.ExistsAsync().ConfigureAwait(true))
        {
            throw new ItemNotFoundException($"document not found at location [{this.Location}]");
        }

        try
        {
            var fs = this.StorageProvider.FileSystem;
            return fs.File.OpenRead(this.Location);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not open document for read at location [{this.Location}]", ex);
        }
    }

    /// <summary>
    /// Renames this storage item.
    /// </summary>
    /// <param name="desiredNewName">The desired new name, which must be valid as required by the underlying storage system (see <see cref="InvalidPathException"/>), should not contain path separators and should not refer to an existing folder or document under the current folder.</param>
    /// <param name="cancellationToken">A cancellation token to observe while waiting for the task to complete.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <exception cref="InvalidPathException">If the <paramref name="desiredNewName"/> is not valid.</exception>
    /// <exception cref="TargetExistsException">If a folder or document already exists under the parent of this folder, with the same name as <paramref name="desiredNewName"/>.</exception>
    public async Task RenameAsync(string desiredNewName, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        // Using the same name => just return
        if (desiredNewName.Equals(this.Name, StringComparison.Ordinal))
        {
            return;
        }

        var fs = this.StorageProvider.FileSystem;

        // The new name must not be a path
        if (!fs.Path.GetFileName(desiredNewName).Equals(desiredNewName, StringComparison.Ordinal))
        {
            throw new InvalidPathException($"the desired new name [{desiredNewName}] is not valid");
        }

        // The new name must be valid within a path string
        var newLocationPath = this.StorageProvider.NormalizeRelativeTo(this.ParentPath, desiredNewName);

        // The new location should not correspond to an existing folder or file
        await this.StorageProvider.EnsureTargetDoesNotExistAsync(newLocationPath, cancellationToken)
            .ConfigureAwait(true);

        if (await this.ExistsAsync().ConfigureAwait(true))
        {
            var parent = await this.StorageProvider.GetFolderFromPathAsync(this.ParentPath, cancellationToken)
                .ConfigureAwait(true);

            await this.MoveAsync(parent, desiredNewName, cancellationToken).ConfigureAwait(true);
        }
        else
        {
            // Just update the file properties
            this.Name = desiredNewName;
            this.Location = newLocationPath;
        }
    }

    private async Task MoveAsync(
        IFolder destinationFolder,
        string desiredNewName,
        bool overwrite,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!await this.ExistsAsync().ConfigureAwait(true))
        {
            throw new ItemNotFoundException($"attempt to move a file that does not exist [{this.Location}]");
        }

        // Determine the destination file path and check if it's the same then the file to be copied
        var destinationFilePath
            = this.StorageProvider.NormalizeRelativeTo(destinationFolder.Location, desiredNewName);
        if (destinationFilePath.Equals(this.Location, StringComparison.Ordinal))
        {
            // No need to fail here, just return this file as-is
            return;
        }

        var fs = this.StorageProvider.FileSystem;

        if (!overwrite && fs.File.Exists(destinationFilePath))
        {
            throw new TargetExistsException(
                $"a document already exists at [{destinationFilePath}], move with no overwrite is not permitted");
        }

        // Ensure destination directory exists, create if necessary
        await destinationFolder.CreateAsync(cancellationToken).ConfigureAwait(true);

        try
        {
            // Use the overload of File.Move that supports the overwrite flag.
            // The move cannot be cancelled!
            fs.File.Move(this.Location, destinationFilePath, overwrite);
        }
        catch (Exception ex)
        {
            throw new StorageException(
                $"could not move document from [{this.Location}] to folder [{destinationFolder}] with desired name [{desiredNewName}] and [overwrite={overwrite}]",
                ex);
        }

        this.Name = fs.Path.GetFileName(destinationFilePath);
        this.Location = destinationFilePath;
        this.ParentPath = destinationFolder.Location;
    }

    private async Task<IDocument> CopyAsync(
        IFolder destinationFolder,
        string desiredNewName,
        bool overwrite,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!await this.ExistsAsync().ConfigureAwait(true))
        {
            throw new ItemNotFoundException($"attempt to copy a file that does not exist [{this.Location}]");
        }

        // Determine the destination file path and check if it's the same then the file to be copied
        var destinationFilePath
            = this.StorageProvider.NormalizeRelativeTo(destinationFolder.Location, desiredNewName);
        if (destinationFilePath.Equals(this.Location, StringComparison.Ordinal))
        {
            // No need to fail here, just return this file as-is
            return this;
        }

        var fs = this.StorageProvider.FileSystem;

        if (!overwrite && fs.File.Exists(destinationFilePath))
        {
            throw new TargetExistsException(
                $"a document already exists at [{destinationFilePath}], copy with no overwrite is not permitted");
        }

        // Ensure destination directory exists, create if necessary
        await destinationFolder.CreateAsync(cancellationToken).ConfigureAwait(true);

        try
        {
            // Use the overload of File.Copy that supports the overwrite flag
            await Task.Run(() => fs.File.Copy(this.Location, destinationFilePath, overwrite), cancellationToken)
                .ConfigureAwait(true);
        }
        catch (TaskCanceledException)
        {
            // Try to delete any partial result of the copy, ignoring any exception that may occur
            TryDeleteFile(destinationFilePath);

            throw;
        }
        catch (Exception ex)
        {
            throw new StorageException(
                $"could not copy document from [{this.Location}] to folder [{destinationFolder}] with desired name [{desiredNewName}] and [overwrite={overwrite}]",
                ex);
        }

        return await this.StorageProvider.GetDocumentFromPathAsync(destinationFilePath, cancellationToken)
            .ConfigureAwait(true);

        void TryDeleteFile(string filePath)
        {
            try
            {
                fs.File.Delete(filePath);
            }
            catch (IOException ioEx)
            {
                Debug.WriteLine(
                    $"failed to delete copy target file at [{filePath}] when copy was cancelled: {ioEx.Message}");
            }
            catch (UnauthorizedAccessException uaEx)
            {
                Debug.WriteLine(
                    $"failed to delete copy target file at [{filePath}] when copy was cancelled: {uaEx.Message}");
            }
        }
    }
}
