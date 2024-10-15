// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

public class NativeFolder : IFolder
{
    internal NativeFolder(NativeStorageProvider storageProvider, string name, string path)
    {
        this.StorageProvider = storageProvider;
        this.Name = name;
        this.Location = path;
    }

    public string Name { get; private set; }

    public string Location { get; private set; }

    public DateTime LastAccessTime
    {
        get
        {
            try
            {
                var fs = this.StorageProvider.FileSystem;
                return fs.Directory.GetLastAccessTime(this.Location);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"An error occurred while getting LastAccessTime for [{this.Location}]: {ex.Message}");
                return DateTime.FromFileTime(0);
            }
        }
    }

    internal NativeStorageProvider StorageProvider { get; }

    public async Task<IDocument> GetDocumentAsync(string documentName, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var fullPath = this.StorageProvider.NormalizeRelativeTo(this.Location, documentName);

        // If the path refers to an existing folder (not file), then we reject it
        if (await this.StorageProvider.FolderExistsAsync(fullPath).ConfigureAwait(false))
        {
            throw new InvalidPathException(
                $"the specified name for a document [{documentName}] refers to an existing folder [{fullPath}]");
        }

        return new NativeFile(
            this.StorageProvider,
            documentName,
            fullPath,
            this.Location);
    }

    public async Task<IFolder> GetFolderAsync(string folderName, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var fullPath = this.StorageProvider.NormalizeRelativeTo(this.Location, folderName);

        // If the path refers to an existing file (not folder), then we reject it
        if (await this.StorageProvider.DocumentExistsAsync(fullPath).ConfigureAwait(false))
        {
            throw new InvalidPathException(
                $"the specified name for a folder [{folderName}] refers to an existing file [{fullPath}]");
        }

        return new NativeNestedFolder(
            this.StorageProvider,
            folderName,
            fullPath,
            this.Location);
    }

    public async Task CreateAsync(CancellationToken cancellationToken = default)
    {
        // Check if the directory already exists
        if (await this.ExistsAsync().ConfigureAwait(false))
        {
            return;
        }

        cancellationToken.ThrowIfCancellationRequested();

        try
        {
            var fs = this.StorageProvider.FileSystem;
            _ = fs.Directory.CreateDirectory(this.Location);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not create folder at location [{this.Location}]", ex);
        }
    }

    public Task<bool> ExistsAsync() => this.StorageProvider.FolderExistsAsync(this.Location);

    public async Task DeleteAsync(CancellationToken cancellationToken = default)
    {
        // Check if the folder does not exists
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            return;
        }

        // Delete the directory
        try
        {
            var fs = this.StorageProvider.FileSystem;
            await Task.Run(() => fs.Directory.Delete(this.Location), cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not delete folder at location [{this.Location}]", ex);
        }
    }

    public async Task DeleteRecursiveAsync(CancellationToken cancellationToken = default)
    {
        // Check if the folder does not exists
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            return;
        }

        // Delete the folder and its content asynchronously
        try
        {
            var fs = this.StorageProvider.FileSystem;
            await Task.Run(() => fs.Directory.Delete(this.Location, recursive: true), cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not recursively delete folder at location [{this.Location}]", ex);
        }
    }

    public async Task RenameAsync(string desiredNewName, CancellationToken cancellationToken = default)
    {
        // Only nested folders (not root folders) can be renamed
        if (this is not INestedItem nestedItem)
        {
            throw new StorageException("only nested folders (with a parent folder) can be renamed");
        }

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
        var newLocationPath = this.StorageProvider.NormalizeRelativeTo(nestedItem.ParentPath, desiredNewName);

        // The new location should not correspond to an existing folder or file
        await this.StorageProvider.EnsureTargetDoesNotExistAsync(newLocationPath, cancellationToken)
            .ConfigureAwait(false);

        if (await this.ExistsAsync().ConfigureAwait(false))
        {
            // Physically rename the directory
            try
            {
                // Run asynchronously because it could take a long time
                await Task.Run(() => fs.Directory.Move(this.Location, newLocationPath), cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                throw new StorageException(
                    $"could not rename folder at location [{this.Location}] to new name [{desiredNewName}]",
                    ex);
            }
        }

        // Update the folder properties
        this.Name = desiredNewName;
        this.Location = newLocationPath;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Roslynator",
        "RCS1227:Validate arguments correctly",
        Justification = "does not fit well with async enumerator")]
    public async IAsyncEnumerable<IFolder> GetFoldersAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        // Ensure the directory exists before starting enumeration
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            throw new StorageException($"cannot enumerate folder at [{this.Location}] if it does not exist");
        }

        var fs = this.StorageProvider.FileSystem;
        var directoryInfo = fs.DirectoryInfo.New(this.Location);
        foreach (var info in directoryInfo.EnumerateDirectories(
                     "*",
                     new EnumerationOptions() { IgnoreInaccessible = true }))
        {
            if (cancellationToken.IsCancellationRequested)
            {
                yield break;
            }

            yield return new NativeNestedFolder(
                this.StorageProvider,
                info.Name,
                info.FullName,
                this.Location);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Roslynator",
        "RCS1227:Validate arguments correctly",
        Justification = "does not fit well with async enumerator")]
    public async IAsyncEnumerable<IDocument> GetDocumentsAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        // Ensure the directory exists before starting enumeration
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            throw new StorageException($"cannot enumerate folder at [{this.Location}] if it does not exist");
        }

        var fs = this.StorageProvider.FileSystem;
        var directoryInfo = fs.DirectoryInfo.New(this.Location);
        foreach (var info in directoryInfo.EnumerateFiles("*", new EnumerationOptions() { IgnoreInaccessible = true }))
        {
            if (cancellationToken.IsCancellationRequested)
            {
                yield break;
            }

            yield return new NativeFile(
                this.StorageProvider,
                info.Name,
                info.FullName,
                this.Location);
        }
    }

    public async IAsyncEnumerable<IStorageItem> GetItemsAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await foreach (var folderItem in this.GetFoldersAsync(cancellationToken)
                           .ConfigureAwait(false))
        {
            yield return folderItem;
        }

        await foreach (var fileItem in this.GetDocumentsAsync(cancellationToken)
                           .ConfigureAwait(false))
        {
            yield return fileItem;
        }
    }

    public async Task<bool> HasItemsAsync()
    {
        // Ensure the directory exists before starting enumeration
        if (!await this.ExistsAsync().ConfigureAwait(false))
        {
            throw new StorageException($"cannot enumerate folder at [{this.Location}] if it does not exist");
        }

        var fs = this.StorageProvider.FileSystem;
        try
        {
            var directoryInfo = fs.DirectoryInfo.New(this.Location);

            // Check for files, including hidden ones
            var files = directoryInfo.GetFiles("*", SearchOption.TopDirectoryOnly);
            if (files.Length > 0)
            {
                return true;
            }

            // Check for directories, including hidden ones
            var directories = directoryInfo.GetDirectories("*", SearchOption.TopDirectoryOnly);
            return directories.Length > 0;
        }
        catch (Exception ex)
        {
            throw new StorageException($"could not check if folder at location [{this.Location}] has items", ex);
        }
    }
}
