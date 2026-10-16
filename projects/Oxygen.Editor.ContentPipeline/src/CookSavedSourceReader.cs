// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads acknowledged saved bytes without retaining live authoring objects.</summary>
internal static partial class CookSavedSourceReader
{
    /// <summary>Reads one saved source while excluding in-place writes and unacknowledged external changes.</summary>
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

    /// <summary>Reads a source file as it is on disk, for status, which describes the files and not open documents.</summary>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The file's bytes.</returns>
    public static async Task<byte[]> ReadFileAsync(string sourcePath, CancellationToken cancellationToken)
    {
        // Like HashFileAsync, never blocks a writer.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = source.ConfigureAwait(false);
        var buffer = new MemoryStream();
        await using var copy = buffer.ConfigureAwait(false);
        await source.CopyToAsync(buffer, cancellationToken).ConfigureAwait(false);
        return buffer.ToArray();
    }

    /// <summary>Hashes a source file as it is on disk, for status, which describes the files and not open documents.</summary>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The file's SHA-256 hash.</returns>
    public static async Task<string> HashFileAsync(string sourcePath, CancellationToken cancellationToken)
    {
        // A status read never blocks a save's atomic replace or a tool writing in place. A write during the read changes
        // the hash, which reads as changed; the write is its own change and reads again.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = source.ConfigureAwait(false);
        return Convert.ToHexString(await SHA256.HashDataAsync(source, cancellationToken).ConfigureAwait(false));
    }

    /// <summary>Copies and hashes one saved source while excluding in-place writes.</summary>
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

    /// <summary>Reads the same source metadata exposed by the native reader, following a link's target.</summary>
    /// <param name="path">The logical source path.</param>
    /// <returns>File or directory metadata with the original link flag.</returns>
    public static Import.NativeSourceFileMetadata ReadMetadata(string path)
    {
        FileSystemInfo source = new FileInfo(path);
        var isLink = source.LinkTarget is not null;
        if (isLink)
        {
            source = source.ResolveLinkTarget(returnFinalTarget: true) ?? throw new IOException($"Cannot resolve source link '{path}'.");
        }

        var directory = source.Attributes.HasFlag(FileAttributes.Directory);
        var modified = new DateTimeOffset(source.LastWriteTimeUtc);
        var seconds = modified.ToUnixTimeSeconds();
        var nanos = checked((int)((modified.Ticks - DateTimeOffset.FromUnixTimeSeconds(seconds).Ticks) * 100));
        return new(directory ? 0 : checked((ulong)new FileInfo(source.FullName).Length), directory, isLink, seconds, nanos);
    }

    /// <summary>Compares a retained presence/metadata probe without opening its contents.</summary>
    /// <param name="input">The probe or absence fact.</param>
    /// <returns>Whether the observed source state is unchanged.</returns>
    public static bool MatchesProbe(CookSnapshotInput input)
    {
        var exists = Exists(input.SourcePath);
        return input.IsAbsent ? !exists : exists && (input.Metadata is null || ReadMetadata(input.SourcePath).Shape == input.Metadata.Shape);
    }

    private static async Task<T> ReadCoreAsync<T>(
        ICookDocumentRegistry documents,
        string sourcePath,
        Func<FileStream, CancellationToken, Task<(T value, string hash)>> read,
        CancellationToken cancellationToken,
        bool allowUnsavedDocuments = false)
    {
        using var reads = await documents.AcquireAsync([sourcePath], cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
        if (!allowUnsavedDocuments && dirty.Length != 0)
        {
            throw new CookInputsNeedSaveException(dirty);
        }

        // In-place writes stay excluded for the complete read/copy. An atomic replace, as a save makes, proceeds: this
        // handle keeps reading the replaced file, whose bytes match the saved state captured under the read leases, and
        // the replacement is a change of its own that is read again.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
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
