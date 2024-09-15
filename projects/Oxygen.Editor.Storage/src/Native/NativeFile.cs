// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;

public class NativeFile : IDocument
{
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

    public string ParentPath { get; private set; }

    public string Name { get; private set; }

    public string Location { get; private set; }

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

    internal NativeStorageProvider StorageProvider { get; }

    public Task<bool> ExistsAsync() => Task.FromResult(this.StorageProvider.FileSystem.File.Exists(this.Location));

    public async Task DeleteAsync(CancellationToken cancellationToken = default)
    {
        // Check if the file does not exists
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            return;
        }

        // Delete the file
        try
        {
            var fs = this.StorageProvider.FileSystem;
            await Task.Run(() => fs.File.Delete(this.Location), cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not delete folder at location [{this.Location}]", ex);
        }
    }

    public Task<IDocument> CopyAsync(IFolder destinationFolder, CancellationToken cancellationToken = default)
        => this.CopyAsync(destinationFolder, this.Name, overwrite: false, cancellationToken);

    public Task<IDocument> CopyAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            desiredNewName,
            overwrite: false,
            cancellationToken);

    public Task<IDocument> CopyOverwriteAsync(
        IFolder destinationFolder,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            this.Name,
            overwrite: true,
            cancellationToken);

    public Task<IDocument> CopyOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.CopyAsync(
            destinationFolder,
            desiredNewName,
            overwrite: true,
            cancellationToken);

    public Task MoveAsync(IFolder destinationFolder, CancellationToken cancellationToken = default)
        => this.MoveAsync(destinationFolder, this.Name, overwrite: false, cancellationToken);

    public Task MoveAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            desiredNewName,
            overwrite: false,
            cancellationToken);

    public Task MoveOverwriteAsync(
        IFolder destinationFolder,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            this.Name,
            overwrite: true,
            cancellationToken);

    public Task MoveOverwriteAsync(
        IFolder destinationFolder,
        string desiredNewName,
        CancellationToken cancellationToken = default)
        => this.MoveAsync(
            destinationFolder,
            desiredNewName,
            overwrite: true,
            cancellationToken);

    public async Task<string> ReadAllTextAsync(CancellationToken cancellationToken = default)
    {
        var fs = this.StorageProvider.FileSystem;
        try
        {
            return await fs.File.ReadAllTextAsync(this.Location, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(false);
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

    public async Task WriteAllTextAsync(string text, CancellationToken cancellationToken = default)
    {
        var fs = this.StorageProvider.FileSystem;
        try
        {
            await fs.File.WriteAllTextAsync(this.Location, text, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not write text content to document [{this.Location}]", ex);
        }
    }

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
            .ConfigureAwait(false);

        if (await this.ExistsAsync().ConfigureAwait(false))
        {
            var parent = await this.StorageProvider.GetFolderFromPathAsync(this.ParentPath, cancellationToken)
                .ConfigureAwait(false);

            await this.MoveAsync(parent, desiredNewName, cancellationToken).ConfigureAwait(false);
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

        if (!await this.ExistsAsync().ConfigureAwait(false))
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
        await destinationFolder.CreateAsync(cancellationToken).ConfigureAwait(false);

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

        if (!await this.ExistsAsync().ConfigureAwait(false))
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
        await destinationFolder.CreateAsync(cancellationToken).ConfigureAwait(false);

        try
        {
            // Use the overload of File.Copy that supports the overwrite flag
            await Task.Run(() => fs.File.Copy(this.Location, destinationFilePath, overwrite), cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TaskCanceledException)
        {
            // Try to delete any partial result of the copy, ignoring any exception that may occur
            try
            {
                fs.File.Delete(destinationFilePath);
            }
            catch (Exception)
            {
                Debug.WriteLine(
                    $"failed to delete copy target file at [{destinationFilePath}] when copy was cancelled");
            }

            throw;
        }
        catch (Exception ex)
        {
            throw new StorageException(
                $"could not copy document from [{this.Location}] to folder [{destinationFolder}] with desired name [{desiredNewName}] and [overwrite={overwrite}]",
                ex);
        }

        return await this.StorageProvider.GetDocumentFromPathAsync(destinationFilePath, cancellationToken)
            .ConfigureAwait(false);
    }
}
