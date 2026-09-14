// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads acknowledged saved bytes without retaining live authoring objects.</summary>
internal static class CookSavedSourceReader
{
    /// <summary>Reads one source while excluding saves and unacknowledged external changes.</summary>
    /// <param name="documents">Registered source owners.</param>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the protected read.</param>
    /// <param name="allowUnsavedDocuments">Reads acknowledged saved bytes for status inspection while an owner has newer unsaved edits.</param>
    /// <returns>The saved bytes released from their source read lease.</returns>
    public static Task<byte[]> ReadAsync(ICookDocumentRegistry documents, string sourcePath, CancellationToken cancellationToken, bool allowUnsavedDocuments = false)
        => ReadCoreAsync(
            documents,
            sourcePath,
            static async (source, token) =>
            {
                var buffer = new MemoryStream();
                await using var lifetime = buffer.ConfigureAwait(false);
                await source.CopyToAsync(buffer, token).ConfigureAwait(false);
                var bytes = buffer.ToArray();
                return (bytes, Convert.ToHexString(SHA256.HashData(bytes)));
            },
            cancellationToken,
            allowUnsavedDocuments);

    /// <summary>Hashes a saved source without loading a potentially large dependency into memory.</summary>
    /// <param name="documents">Registered source owners.</param>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the protected read.</param>
    /// <param name="allowUnsavedDocuments">Reads acknowledged saved bytes for read-only status while the document has newer edits.</param>
    /// <returns>The acknowledged source's SHA-256 hash.</returns>
    public static Task<string> HashAsync(ICookDocumentRegistry documents, string sourcePath, CancellationToken cancellationToken, bool allowUnsavedDocuments = false)
        => ReadCoreAsync(
            documents,
            sourcePath,
            static async (source, token) =>
            {
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(source, token).ConfigureAwait(false));
                return (hash, hash);
            },
            cancellationToken,
            allowUnsavedDocuments);

    /// <summary>Copies and hashes one saved source while excluding saves and external replacement.</summary>
    /// <param name="documents">Registered source owners.</param>
    /// <param name="sourcePath">The original absolute source path.</param>
    /// <param name="destinationPath">A new private discovery copy.</param>
    /// <param name="cancellationToken">Cancels the protected copy.</param>
    /// <returns>The hash of the exact bytes copied.</returns>
    public static Task<string> CopyAsync(ICookDocumentRegistry documents, string sourcePath, string destinationPath, CancellationToken cancellationToken)
        => ReadCoreAsync(
            documents,
            sourcePath,
            async (source, token) =>
            {
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(source, token).ConfigureAwait(false));
                source.Position = 0;
                var output = new FileStream(destinationPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous);
                await using var lifetime = output.ConfigureAwait(false);
                await source.CopyToAsync(output, token).ConfigureAwait(false);
                return (hash, hash);
            },
            cancellationToken);

    /// <summary>Checks presence without treating inaccessible input as an absent optional file.</summary>
    /// <remarks>FileSystemInfo reports missing entries with -1 while preserving errors reading attributes.</remarks>
    /// <param name="path">The input path.</param>
    /// <returns>Whether a filesystem entry exists at the path.</returns>
    public static bool Exists(string path)
        => new FileInfo(path).Attributes != (FileAttributes)(-1);

    private static async Task<T> ReadCoreAsync<T>(
        ICookDocumentRegistry documents,
        string sourcePath,
        Func<FileStream, CancellationToken, Task<(T value, string hash)>> read,
        CancellationToken cancellationToken,
        bool allowUnsavedDocuments = false)
    {
        using var reads = await documents.AcquireAsync([sourcePath], cancellationToken).ConfigureAwait(false);
        var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
        if (!allowUnsavedDocuments && dirty.Length != 0)
        {
            throw new CookInputsNeedSaveException(dirty);
        }

        // Keep external writes and replacement excluded for the complete read/copy.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = source.ConfigureAwait(false);
        var (value, hash) = await read(source, cancellationToken).ConfigureAwait(false);
        foreach (var document in reads.Documents)
        {
            if (!string.Equals(hash, document.SavedContentHash, StringComparison.OrdinalIgnoreCase))
            {
                throw new IOException($"'{document.DisplayName}' changed outside the editor. Reload it before cooking. Source: {sourcePath}");
            }
        }

        return value;
    }
}
