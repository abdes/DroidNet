// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Complete byte identities used to seed and verify one publication root.</summary>
/// <param name="Exists">Whether the root existed.</param>
/// <param name="Files">The complete root-relative file set.</param>
internal sealed record CookRootImage(bool Exists, ImmutableSortedDictionary<string, CookRootImage.FileImage> Files)
{
    /// <summary>Reads a root or copies it to private staging while recording the same bytes.</summary>
    /// <param name="root">The published or staged root.</param>
    /// <param name="copyTo">Optional new staging destination.</param>
    /// <param name="cancellationToken">Cancels before publication.</param>
    /// <returns>The protected source content identities.</returns>
    public static async Task<CookRootImage> CaptureAsync(string root, string? copyTo, CancellationToken cancellationToken)
    {
        var files = ImmutableSortedDictionary.CreateBuilder<string, FileImage>(StringComparer.Ordinal);
        CookOutputLease.RejectReparsePoint(root);
        var rootAttributes = new DirectoryInfo(root).Attributes;
        if (rootAttributes == (FileAttributes)(-1))
        {
            return new(Exists: false, files.ToImmutable());
        }

        if (!rootAttributes.HasFlag(FileAttributes.Directory))
        {
            throw new IOException($"A cooked root is not a directory: '{root}'.");
        }

        foreach (var path in EnumerateFiles(root))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
            var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using var sourceLifetime = source.ConfigureAwait(false);
            var lastWrite = File.GetLastWriteTimeUtc(path);
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(source, cancellationToken).ConfigureAwait(false));
            files.Add(relative, new(source.Length, hash, lastWrite));
            if (copyTo is not null)
            {
                var target = Path.Combine(copyTo, relative);
                _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                var destination = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous);
                await using (destination.ConfigureAwait(false))
                {
                    source.Position = 0;
                    await source.CopyToAsync(destination, cancellationToken).ConfigureAwait(false);
                }

                File.SetLastWriteTimeUtc(target, lastWrite);
            }
        }

        return new(Exists: true, files.ToImmutable());
    }

    /// <summary>Compares bytes and membership without treating timestamps as proof of freshness.</summary>
    /// <param name="other">The independently observed root.</param>
    /// <returns>Whether both complete content sets match.</returns>
    public bool Matches(CookRootImage other)
        => this.Exists == other.Exists && this.Files.Count == other.Files.Count
            && this.Files.All(pair => other.Files.TryGetValue(pair.Key, out var file)
                && file.Size == pair.Value.Size && string.Equals(file.Sha256, pair.Value.Sha256, StringComparison.Ordinal));

    private static IEnumerable<string> EnumerateFiles(string root)
    {
        var directories = new Stack<string>();
        directories.Push(root);
        while (directories.TryPop(out var directory))
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(directory).Order(StringComparer.Ordinal))
            {
                CookOutputLease.RejectReparsePoint(entry);
                if (File.GetAttributes(entry).HasFlag(FileAttributes.Directory))
                {
                    directories.Push(entry);
                }
                else
                {
                    yield return entry;
                }
            }
        }
    }

    /// <summary>A file's content and preserved modification time.</summary>
    /// <param name="Size">The full byte length.</param>
    /// <param name="Sha256">The SHA-256 digest.</param>
    /// <param name="LastWriteUtc">The timestamp preserved when seeding unchanged files.</param>
    public sealed record FileImage(long Size, string Sha256, DateTime LastWriteUtc);
}
