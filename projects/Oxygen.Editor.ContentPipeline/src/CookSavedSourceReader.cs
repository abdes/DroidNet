// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads the saved source files a cook consumes, as they are on disk.</summary>
internal static partial class CookSavedSourceReader
{
    /// <summary>Reads one saved source while excluding in-place writes.</summary>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The saved bytes.</returns>
    public static Task<byte[]> ReadAsync(string sourcePath, CancellationToken cancellationToken)
        => ReadCoreAsync(
            sourcePath,
            static async (source, token) =>
            {
                var buffer = new MemoryStream();
                await using var lifetime = buffer.ConfigureAwait(false);
                await source.CopyToAsync(buffer, token).ConfigureAwait(false);
                var bytes = buffer.ToArray();
                return bytes;
            },
            cancellationToken);

    /// <summary>Hashes a saved source without loading a potentially large dependency into memory.</summary>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The source's SHA-256 hash.</returns>
    public static Task<string> HashAsync(string sourcePath, CancellationToken cancellationToken)
        => ReadCoreAsync(
            sourcePath,
            static async (source, token) => Convert.ToHexString(await SHA256.HashDataAsync(source, token).ConfigureAwait(false)),
            cancellationToken);

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
    /// <param name="sourcePath">The original absolute source path.</param>
    /// <param name="destinationPath">A new private discovery copy.</param>
    /// <param name="cancellationToken">Cancels the copy.</param>
    /// <returns>The hash of the exact bytes copied.</returns>
    public static Task<string> CopyAsync(string sourcePath, string destinationPath, CancellationToken cancellationToken)
        => ReadCoreAsync(
            sourcePath,
            async (source, token) =>
            {
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(source, token).ConfigureAwait(false));
                source.Position = 0;
                var output = new FileStream(destinationPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous);
                await using var lifetime = output.ConfigureAwait(false);
                await source.CopyToAsync(output, token).ConfigureAwait(false);
                return hash;
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
        var exists = Exists(input.SourcePath.FullPath);
        return input.IsAbsent ? !exists : exists && (input.Metadata is null || ReadMetadata(input.SourcePath.FullPath).Shape == input.Metadata.Shape);
    }

    private static async Task<T> ReadCoreAsync<T>(
        string sourcePath,
        Func<FileStream, CancellationToken, Task<T>> read,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        // In-place writes stay excluded for the complete read/copy. An atomic replace, as a save makes, proceeds: this
        // handle keeps reading the replaced file, and the replacement is a change of its own that is read again.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = source.ConfigureAwait(false);
        return await read(source, cancellationToken).ConfigureAwait(false);
    }
}
