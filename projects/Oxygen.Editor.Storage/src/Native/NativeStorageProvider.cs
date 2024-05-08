// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;

public class NativeStorageProvider(IFileSystem fs) : IStorageProvider
{
    private readonly IFileSystem fs = fs;

    public IEnumerable<string> GetLogicalDrives() => this.fs.Directory.GetLogicalDrives();

    public Task<IFolder> GetFolderFromPathAsync(string path, CancellationToken cancellationToken = default)
    {
        try
        {
            var normalized = this.fs.Path.GetFullPath($"{path}");
            if (!this.fs.Directory.Exists(normalized))
            {
                Debug.WriteLine($"Folder at path [{normalized}] does not exist");
                throw new FileNotFoundException();
            }

            var info = this.fs.DirectoryInfo.New(normalized);
            var folderName = info.Name;
            Debug.Assert(folderName != null, $"Path [{normalized}] did not produce a valid folder name");

            var dateModified = info.LastAccessTime;

            var parent = this.fs.Directory.GetParent(normalized);

            var folder = parent == null
                ? new NativeFolder(this, folderName, normalized, dateModified)
                : new NativeNestedFolder(this, folderName, normalized, parent.FullName, dateModified);

            return Task.FromResult<IFolder>(folder);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Could not make a folder out of path [{path}]: {ex.Message}");
            throw;
        }
    }

    public async IAsyncEnumerable<IStorageItem> GetItemsAsync(
        string path,
        ProjectItemKind kind = ProjectItemKind.All,
        [EnumeratorCancellation]
        CancellationToken cancellationToken = default)
    {
        path = Path.GetFullPath(path);

        await foreach (var folderItem in this.EnumerateFoldersAsync(path, kind, cancellationToken).ConfigureAwait(true))
        {
            yield return folderItem;
        }

        await foreach (var fileItem in this.EnumerateFilesAsync(path, kind, cancellationToken).ConfigureAwait(true))
        {
            yield return fileItem;
        }
    }

    private async IAsyncEnumerable<IStorageItem> EnumerateFoldersAsync(
        string path,
        ProjectItemKind kind,
        [EnumeratorCancellation]
        CancellationToken cancellationToken)
    {
        if ((kind & ProjectItemKind.Folder) == ProjectItemKind.Folder)
        {
            Debug.WriteLine($"Enumerating folders under `{path}`");

            foreach (var item in this.fs.Directory.EnumerateDirectories(path))
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    yield break;
                }

                var itemPath = this.fs.Path.Combine(path, item);
                var info = this.fs.DirectoryInfo.New(itemPath);
                var dateModified = info.LastAccessTime;
                yield return new NativeNestedFolder(this, info.Name, itemPath, path, dateModified);
            }
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }

    private async IAsyncEnumerable<IStorageItem> EnumerateFilesAsync(
        string path,
        ProjectItemKind kind,
        [EnumeratorCancellation]
        CancellationToken cancellationToken)
    {
        if ((kind & ProjectItemKind.File) == ProjectItemKind.File)
        {
            var extensions = new List<string>();
            if ((kind & ProjectItemKind.ProjectManifest) == ProjectItemKind.ProjectManifest)
            {
                extensions.Add("oxy");
            }

            var pattern = "^.*";
            if (extensions.Count > 0)
            {
                pattern += @"\.(";
            }

            var firstTime = true;
            foreach (var extension in extensions)
            {
                if (!firstTime)
                {
                    pattern += "|";
                    firstTime = false;
                }

                pattern += extension;
            }

            if (extensions.Count > 0)
            {
                pattern += ")";
            }

            pattern += "$";

            Debug.WriteLine($"Enumerating files under `{path}` matching pattern `{pattern}`");
            var regex = new Regex(
                pattern,
                RegexOptions.None,
                TimeSpan.FromSeconds(1)); // protect regex against denial of service attacks

            foreach (var item in this.fs.Directory.EnumerateFiles(path))
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    yield break;
                }

                if (!regex.IsMatch(item.ToLowerInvariant()))
                {
                    continue;
                }

                var itemPath = this.fs.Path.Combine(path, item);
                var info = this.fs.FileInfo.New(itemPath);
                var dateModified = info.LastAccessTime;
                yield return new NativeFile(this, info.Name, itemPath, path, dateModified);
            }
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
