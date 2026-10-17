// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using DroidNet.Storage;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// Files in memory, with the store's contract: a version is the content's SHA-256, and a write whose expected version
/// is not the file's current one is a conflict.
/// </summary>
public sealed class MemoryFileStore : IAtomicFileStore
{
    private readonly Dictionary<string, ImmutableArray<byte>> files = [with(StringComparer.OrdinalIgnoreCase)];

    /// <summary>
    /// Puts a file in the store, as an outside tool would.
    /// </summary>
    /// <param name="path">The file's path.</param>
    /// <param name="content">Its content.</param>
    public void Put(string path, ReadOnlySpan<byte> content) => this.files[path] = [.. content];

    /// <inheritdoc/>
    public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
        => Task.FromResult(this.files.TryGetValue(path, out var content)
            ? new FileSnapshot(content, VersionOf(content))
            : new FileSnapshot([], FileVersion.Missing));

    /// <inheritdoc/>
    public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(expected);
        var current = this.files.TryGetValue(path, out var existing) ? VersionOf(existing) : FileVersion.Missing;
        if (current != expected)
        {
            throw new StorageWriteConflictException($"'{path}' changed since it was read.");
        }

        ImmutableArray<byte> written = [.. content.Span];
        this.files[path] = written;
        return Task.FromResult(VersionOf(written));
    }

    private static FileVersion VersionOf(ImmutableArray<byte> content)
        => new(Exists: true, Convert.ToHexString(SHA256.HashData(content.AsSpan())));
}
