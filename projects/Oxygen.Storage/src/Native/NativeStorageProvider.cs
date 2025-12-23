// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.IO.Abstractions;

namespace Oxygen.Storage.Native;

/// <summary>
/// Provides methods for managing storage items (folders and documents) in the file system.
/// </summary>
public class NativeStorageProvider(IFileSystem fs) : IStorageProvider
{
    /// <summary>
    /// Gets the file system abstraction used by this storage provider.
    /// </summary>
    internal IFileSystem FileSystem => fs;

    /// <summary>
    /// Gets the logical drives available on the system.
    /// </summary>
    /// <returns>An enumerable collection of logical drive names.</returns>
    public IEnumerable<string> GetLogicalDrives() => fs.Directory.GetLogicalDrives();

    /// <inheritdoc />
    public Task<IFolder> GetFolderFromPathAsync(
        string folderPath,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var normalized = this.Normalize(folderPath);

        // If the path refers to an existing file (not folder), then we reject it
        if (fs.File.Exists(normalized))
        {
            throw new InvalidPathException(
                $"the specified path for a folder [{folderPath}] refers to an existing file");
        }

        // If we cannot extract a folder name from the path, then this is a
        // root folder and we should use the full path as a name.
        var folderName = fs.Path.GetFileName(normalized);
        if (string.IsNullOrEmpty(folderName))
        {
            folderName = normalized;
        }

        var parent = fs.Path.GetDirectoryName(normalized);

        var folder = parent is null
            ? new NativeFolder(this, folderName, normalized)
            : new NativeNestedFolder(this, folderName, normalized, parent);

        return Task.FromResult<IFolder>(folder);
    }

    /// <inheritdoc />
    public Task<IDocument> GetDocumentFromPathAsync(string documentPath, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var normalized = this.Normalize(documentPath);

        // If the path refers to an existing item and it's a folder, then we reject it
        if (fs.Directory.Exists(normalized))
        {
            throw new InvalidPathException(
                $"the specified path for a document [{documentPath}] refers to an existing folder");
        }

        // The path is normalized, not empty and does not refer to an existing folder.
        var fileName = fs.Path.GetFileName(normalized);
        var parent = fs.Path.GetDirectoryName(normalized);
        Debug.Assert(
            parent is not null,
            "if the path is absolute and has a file name, it should always have a directory name");

        var document = new NativeFile(this, fileName, normalized, parent);

        return Task.FromResult<IDocument>(document);
    }

    /// <inheritdoc />
    public string Normalize(string path)
    {
        CheckForInvalidCharacters(path);

        try
        {
            var qualifiedPath = fs.Path.GetFullPath(path);
            return StripTrailingSeparators(qualifiedPath);
        }
        catch (Exception ex)
        {
            throw new InvalidPathException($"path [{path}] could not be normalized because it is invalid", ex);
        }
    }

    /// <inheritdoc />
    public string NormalizeRelativeTo(string basePath, string relativePath)
    {
        if (fs.Path.IsPathRooted(relativePath))
        {
            throw new InvalidPathException($"path [{relativePath}] is rooted and cannot be used as a relative path");
        }

        try
        {
            var combined = fs.Path.Combine(basePath, relativePath);
            return this.Normalize(combined);
        }
        catch (Exception ex)
        {
            throw new InvalidPathException(
                $"could not combine [{basePath}] and [{relativePath}] because one of them is invalid",
                ex);
        }
    }

    /// <inheritdoc />
    public Task<bool> DocumentExistsAsync(string path) => Task.FromResult(fs.File.Exists(path));

    /// <inheritdoc />
    public Task<bool> FolderExistsAsync(string path) => Task.FromResult(fs.Directory.Exists(path));

    /// <summary>
    /// Checks if the specified <paramref name="path"/> corresponds to an existing folder or document, and throws a <see cref="TargetExistsException"/> if it does.
    /// </summary>
    /// <param name="path">The path to be checked.</param>
    /// <param name="cancellationToken">A cancellation token to observe while waiting for the task to complete.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <exception cref="TargetExistsException">If the specified <paramref name="path"/> refers to an existing document or folder.</exception>
    internal async Task EnsureTargetDoesNotExistAsync(string path, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var maybeDocument = await this.DocumentExistsAsync(path).ConfigureAwait(true);
        var maybeFolder = await this.FolderExistsAsync(path).ConfigureAwait(true);

        if (maybeDocument || maybeFolder)
        {
            throw new TargetExistsException($"location [{path}] corresponds to an existing item");
        }
    }

    /// <summary>
    /// Checks for invalid characters in the specified path.
    /// </summary>
    /// <param name="path">The path to be checked.</param>
    /// <exception cref="InvalidPathException">Thrown if the path contains invalid characters.</exception>
    private static void CheckForInvalidCharacters(string path)
    {
        // Check for invalid characters
        var invalidChars = Path.GetInvalidPathChars();
        foreach (var c in path)
        {
            if (Array.Exists(invalidChars, invalidChar => invalidChar == c))
            {
                var printable = string.Create(CultureInfo.InvariantCulture, $"{(int)c:X2}");
                throw new InvalidPathException(
                    $"path [{path}] could not be normalized because it contains an invalid character[0x{printable}]");
            }
        }
    }

    /// <summary>
    /// Strips trailing separators from the specified path.
    /// </summary>
    /// <param name="path">The path to be stripped of trailing separators.</param>
    /// <returns>The path without trailing separators.</returns>
    private static string StripTrailingSeparators(string path)
    {
        Debug.Assert(!string.IsNullOrEmpty(path), "expecting IsNullOrEmpty to be already checked by GetFullPath");

        var lastChar = path[^1];
        if ((lastChar == Path.DirectorySeparatorChar || lastChar == Path.AltDirectorySeparatorChar) &&
            path.Length > 1)
        {
            var trimmedPath = path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

            // We should not trim if that's the only separator in the path string.
            // For example, "C:/" should stay "C:/" and "/" should stay "/"
            if (trimmedPath.IndexOfAny([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar]) != -1)
            {
                return trimmedPath;
            }
        }

        return path;
    }
}
