// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.IO.Abstractions;
using System.Security.Cryptography;

namespace DroidNet.Storage.Native;

/// <summary>Publishes flushed same-directory files without truncating the current destination.</summary>
/// <param name="fileSystem">The filesystem implementation.</param>
public sealed class NativeAtomicFileStore(IFileSystem fileSystem) : IAtomicFileStore
{
    // A reader that opens the destination without sharing delete, as most file APIs do, blocks its replacement while
    // it reads: Windows denies the replacement access. Such a read ends within moments, so the replacement waits for
    // it, about 0.8 s in all, before the save reports a conflict.
    private static readonly TimeSpan[] SharingRetryDelays =
    [
        TimeSpan.FromMilliseconds(10),
        TimeSpan.FromMilliseconds(25),
        TimeSpan.FromMilliseconds(50),
        TimeSpan.FromMilliseconds(100),
        TimeSpan.FromMilliseconds(200),
        TimeSpan.FromMilliseconds(400),
    ];

    private readonly IFileSystem fileSystem = fileSystem;
    private readonly Func<AtomicWriteStage, Stream?, CancellationToken, Task> beforeStage = (_, _, _) => Task.CompletedTask;

    /// <summary>Initializes a new instance of the <see cref="NativeAtomicFileStore"/> class with a controlled I/O boundary.</summary>
    /// <param name="fileSystem">The filesystem implementation.</param>
    /// <param name="beforeStage">A storage-stage interceptor used for deterministic failure validation.</param>
    internal NativeAtomicFileStore(IFileSystem fileSystem, Func<AtomicWriteStage, Stream?, CancellationToken, Task> beforeStage)
        : this(fileSystem)
    {
        this.beforeStage = beforeStage;
    }

    /// <inheritdoc/>
    public async Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        cancellationToken.ThrowIfCancellationRequested();
        if (this.fileSystem.FileInfo.New(path).Attributes == (FileAttributes)(-1))
        {
            return new([], FileVersion.Missing);
        }

        try
        {
            // Sharing delete lets a save replace the file while it is read; the read keeps the bytes it opened.
            var source = this.fileSystem.FileStream.New(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, 4096, FileOptions.Asynchronous);
            await using var sourceLifetime = source.ConfigureAwait(false);
            var content = new MemoryStream();
            await using var contentLifetime = content.ConfigureAwait(false);
            await source.CopyToAsync(content, cancellationToken).ConfigureAwait(false);
            var bytes = content.ToArray();
            return new(bytes.ToImmutableArray(), Version(bytes));
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
        {
            return new([], FileVersion.Missing);
        }
    }

    /// <inheritdoc/>
    public async Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(expected);
        cancellationToken.ThrowIfCancellationRequested();
        var bytes = content.ToArray();
        var destination = this.fileSystem.Path.GetFullPath(path);
        var directory = this.fileSystem.Path.GetDirectoryName(destination)!;
        _ = this.fileSystem.Directory.CreateDirectory(directory);
        var lease = this.AcquireLease(destination);
        await using var leaseLifetime = lease.ConfigureAwait(false);
        await this.VerifyBaselineAsync(destination, expected, cancellationToken).ConfigureAwait(false);
        var temporary = this.fileSystem.Path.Combine(directory, $".oxygen-{Guid.NewGuid():N}.tmp");
        var owned = false;
        try
        {
            var stream = this.fileSystem.FileStream.New(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.Asynchronous | FileOptions.WriteThrough);
            await using (var streamLifetime = stream.ConfigureAwait(false))
            {
                owned = true;
                await this.beforeStage(AtomicWriteStage.BeforeWrite, stream, cancellationToken).ConfigureAwait(false);
                await stream.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
                await this.beforeStage(AtomicWriteStage.BeforeFlush, stream, cancellationToken).ConfigureAwait(false);
                await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
                await Task.Run(() => stream.Flush(flushToDisk: true), cancellationToken).ConfigureAwait(false);
            }

            await this.beforeStage(AtomicWriteStage.BeforeReplace, arg2: null, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            await this.VerifyBaselineAsync(destination, expected, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            await this.ReplaceAsync(temporary, destination, expected, cancellationToken).ConfigureAwait(false);
            owned = false;
            return Version(bytes);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or OperationCanceledException)
        {
            if (owned)
            {
                try
                {
                    this.fileSystem.File.Delete(temporary);
                }
                catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException)
                {
                    exception.Data["RetainedTemporaryPath"] = temporary;
                    exception.Data["TemporaryCleanupFailure"] = cleanup.Message;
                }
            }

            throw;
        }
    }

    private static FileVersion Version(ReadOnlySpan<byte> bytes)
        => new(Exists: true, Convert.ToHexString(SHA256.HashData(bytes)));

    // Another handle has the destination open: access denied, ERROR_SHARING_VIOLATION or ERROR_LOCK_VIOLATION.
    private static bool IsHeldOpen(Exception exception)
        => exception is UnauthorizedAccessException || (exception is IOException && (exception.HResult & 0xffff) is 32 or 33);

    private async Task ReplaceAsync(string temporary, string destination, FileVersion expected, CancellationToken cancellationToken)
    {
        for (var attempt = 0; ; attempt++)
        {
            try
            {
                this.fileSystem.File.Move(temporary, destination, overwrite: expected.Exists);
                return;
            }
            catch (Exception exception) when (IsHeldOpen(exception) && attempt < SharingRetryDelays.Length)
            {
                await Task.Delay(SharingRetryDelays[attempt], cancellationToken).ConfigureAwait(false);

                // The file may have changed while the reader held it.
                await this.VerifyBaselineAsync(destination, expected, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is UnauthorizedAccessException
                || (exception is IOException && (exception.HResult & 0xffff) is 32 or 33 or 80 or 183))
            {
                throw new StorageWriteConflictException("The destination became inaccessible or another writer created it before replacement.", exception);
            }
        }
    }

    private FileSystemStream AcquireLease(string path)
    {
        try
        {
            return this.fileSystem.FileStream.New(path + ".oxygen-write.lock", FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new StorageWriteConflictException("Another writer owns the destination or its write lease is unavailable.", exception);
        }
    }

    private async Task VerifyBaselineAsync(string path, FileVersion expected, CancellationToken cancellationToken)
    {
        FileVersion actual;
        try
        {
            actual = (await this.ReadAsync(path, cancellationToken).ConfigureAwait(false)).Version;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new StorageWriteConflictException("The current destination could not be compared with the opened version.", exception);
        }

        if (actual != expected)
        {
            throw new StorageWriteConflictException("The destination changed outside this document. Reload it, save a copy, or keep your edits.");
        }
    }
}
