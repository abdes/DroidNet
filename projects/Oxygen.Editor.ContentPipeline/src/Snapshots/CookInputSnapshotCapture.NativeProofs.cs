// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Checks native observations against the private copy before accepting a snapshot.</summary>
public sealed partial class CookInputSnapshotCapture
{
    /// <summary>Admits operation-owned generated descriptors without copying them into authored provenance.</summary>
    /// <param name="sources">The generated descriptors to admit.</param>
    /// <param name="cancellationToken">Cancels metadata and content reads.</param>
    /// <returns>The captured generated-input identities.</returns>
    internal static async Task<ImmutableArray<NativeCapturedInput>> CaptureGeneratedAsync(IEnumerable<ContentCookInput> sources, CancellationToken cancellationToken)
    {
        var captured = ImmutableArray.CreateBuilder<NativeCapturedInput>();
        foreach (var source in sources.DistinctBy(static input => input.SourceAbsolutePath, StringComparer.OrdinalIgnoreCase))
        {
            var stream = new FileStream(source.SourceAbsolutePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous);
            await using var lifetime = stream.ConfigureAwait(false);
            var metadata = ReadNativeMetadata(source.SourceAbsolutePath, stream);
            var hash = Convert.ToHexStringLower(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false));
            captured.Add(new(source.SourceAbsolutePath, true, metadata, new(source.SourceAbsolutePath, metadata.Size, hash)));
        }

        return captured.ToImmutable();
    }

    private static bool MatchesCapturedInput(CookSnapshotInput input, CapturedSource source)
        => input.Kind switch
        {
            CookSnapshotInputKind.Absent => !source.Exists,
            CookSnapshotInputKind.Probe => source.Exists && (input.Metadata is null || input.Metadata == source.Metadata),
            CookSnapshotInputKind.File => source.Exists && string.Equals(source.Hash, input.DiscoveryHash, StringComparison.Ordinal),
            _ => false,
        };

    private static CapturedSource ReadProbe(CookSnapshotInput input)
    {
        var exists = CookSavedSourceReader.Exists(input.SourcePath);
        var needsMetadata = input.Metadata is not null || input.NativeObservations.Any(static observation => observation.Metadata is not null);
        return new(string.Empty, exists, exists && needsMetadata ? CookSavedSourceReader.ReadMetadata(input.SourcePath) : null);
    }

    private static async Task<bool> VerifyNativeObservationsAsync(
        ImmutableArray<CookSnapshotInput> inputs,
        string capturedRoot,
        Dictionary<string, CapturedSource> captured,
        CancellationToken cancellationToken)
    {
        foreach (var input in inputs)
        {
            cancellationToken.ThrowIfCancellationRequested();
            foreach (var observation in input.NativeObservations)
            {
                var source = captured[input.SourcePath];
                if (observation.Exists == input.IsAbsent || observation.Exists != source.Exists)
                {
                    return false;
                }

                if (!observation.Exists)
                {
                    continue;
                }

                if (observation.Metadata is { } expected && source.Metadata != expected)
                {
                    return false;
                }

                var capturedPath = Path.Combine(capturedRoot, input.RelativePath);
                foreach (var read in observation.Reads)
                {
                    var digest = read.Offset == 0 && (read.MaxBytes == 0 || read.MaxBytes >= source.Metadata!.Size)
                        ? source.Hash
                        : await HashRangeAsync(capturedPath, read, cancellationToken).ConfigureAwait(false);
                    if (!string.Equals(digest, read.Sha256, StringComparison.OrdinalIgnoreCase))
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    private static NativeSourceFileMetadata ReadNativeMetadata(string path, FileStream source)
    {
        var isLink = new FileInfo(path).LinkTarget is not null;
        var isDirectory = File.GetAttributes(source.SafeFileHandle).HasFlag(FileAttributes.Directory);
        var modified = new DateTimeOffset(File.GetLastWriteTimeUtc(source.SafeFileHandle));
        var seconds = modified.ToUnixTimeSeconds();
        var nanoseconds = checked((int)((modified.Ticks - DateTimeOffset.FromUnixTimeSeconds(seconds).Ticks) * 100));
        return new(checked((ulong)source.Length), isDirectory, isLink, seconds, nanoseconds);
    }

    private static async Task<string> HashRangeAsync(string path, NativeSourceReadProof read, CancellationToken cancellationToken)
    {
        const int bufferSize = 64 * 1024;
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, bufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = stream.ConfigureAwait(false);
        stream.Position = (long)Math.Min(read.Offset, (ulong)stream.Length);
        var remaining = (ulong)(stream.Length - stream.Position);
        if (read.MaxBytes != 0)
        {
            remaining = Math.Min(remaining, read.MaxBytes);
        }

        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = ArrayPool<byte>.Shared.Rent(bufferSize);
        try
        {
            while (remaining != 0)
            {
                var count = await stream.ReadAsync(buffer.AsMemory(0, (int)Math.Min(remaining, (ulong)bufferSize)), cancellationToken).ConfigureAwait(false);
                if (count == 0)
                {
                    throw new EndOfStreamException("Captured source ended before its verified range.");
                }

                hash.AppendData(buffer, 0, count);
                remaining -= (ulong)count;
            }

            return Convert.ToHexString(hash.GetHashAndReset());
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }

    private sealed record CapturedSource(string Hash, bool Exists, NativeSourceFileMetadata? Metadata);
}
