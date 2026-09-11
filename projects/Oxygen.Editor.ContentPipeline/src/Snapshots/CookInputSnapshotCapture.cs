// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Copies a stable dependency set while protecting saved documents from overlapping writes.</summary>
/// <param name="documents">The registered authoring owners and their save gates.</param>
/// <param name="coordinator">The owning project lifetime.</param>
public sealed class CookInputSnapshotCapture(ICookDocumentRegistry documents, IContentCookCoordinator coordinator)
{
    /// <summary>Discovers and captures coherent saved bytes, retrying changed discovery at most three times.</summary>
    /// <param name="operation">The operation holding the project writer.</param>
    /// <param name="discover">Resolves the complete source closure and its discovery hashes.</param>
    /// <param name="buildFingerprint">The qualified native/tool/schema fingerprint.</param>
    /// <param name="cancellationToken">Cancels capture before native work begins.</param>
    /// <returns>A snapshot or documents requiring Save/Reload.</returns>
    public async Task<CookSnapshotCaptureResult> CaptureAsync(
        ContentCookOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<CookSnapshotInput>>> discover,
        string buildFingerprint,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(discover);
        ArgumentException.ThrowIfNullOrWhiteSpace(buildFingerprint);
        var operationRoot = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"));
        var inputRoot = Path.Combine(operationRoot, "inputs");
        EnsureNewDirectory(inputRoot);

        for (var attempt = 0; attempt < 3; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            coordinator.VerifyWriter(operation);
            var inputs = NormalizeInputs(await discover(cancellationToken).ConfigureAwait(false));
            using var reads = await documents.AcquireAsync(inputs.Select(static input => input.SourcePath), cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            coordinator.VerifyWriter(operation);
            var dirty = reads.Documents.Where(static document => document.IsDirty).ToImmutableArray();
            if (!dirty.IsEmpty)
            {
                return new(Snapshot: null, dirty, []);
            }

            var attemptRoot = Path.Combine(operationRoot, "capture-" + attempt.ToString(System.Globalization.CultureInfo.InvariantCulture));
            EnsureNewDirectory(attemptRoot);
            Directory.CreateDirectory(attemptRoot);
            try
            {
                var hashes = await CopyInputsAsync(inputs, attemptRoot, cancellationToken).ConfigureAwait(false);
                if (inputs.Any(input => !string.Equals(hashes[input.SourcePath], input.DiscoveryHash, StringComparison.Ordinal)))
                {
                    continue;
                }

                var changed = reads.Documents.Where(document =>
                    !string.Equals(hashes[Path.GetFullPath(document.SourcePath)], document.SavedContentHash, StringComparison.OrdinalIgnoreCase)).ToImmutableArray();
                if (!changed.IsEmpty)
                {
                    return new(Snapshot: null, [], changed);
                }

                cancellationToken.ThrowIfCancellationRequested();
                coordinator.VerifyWriter(operation);
                Directory.Move(attemptRoot, inputRoot);
                var identity = ComputeIdentity(buildFingerprint, inputs);
                return new(new(operation, inputRoot, buildFingerprint, identity, inputs, reads.Documents), [], []);
            }
            finally
            {
                if (Directory.Exists(attemptRoot))
                {
                    Directory.Delete(attemptRoot, recursive: true);
                }
            }
        }

        throw new IOException("Cook inputs changed during dependency discovery three times. Retry after source writes finish.");
    }

    private static void EnsureNewDirectory(string path)
    {
        if (Directory.Exists(path))
        {
            throw new InvalidOperationException("The operation's input capture directory already exists.");
        }
    }

    private static async Task<Dictionary<string, string>> CopyInputsAsync(
        ImmutableArray<CookSnapshotInput> inputs,
        string attemptRoot,
        CancellationToken cancellationToken)
    {
        var streams = new Dictionary<string, FileStream>(StringComparer.OrdinalIgnoreCase);
        try
        {
            foreach (var input in inputs.OrderBy(static input => input.SourcePath, StringComparer.OrdinalIgnoreCase))
            {
                if (!streams.ContainsKey(input.SourcePath))
                {
                    streams.Add(input.SourcePath, new FileStream(input.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan));
                }
            }

            var hashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var input in inputs)
            {
                var source = streams[input.SourcePath];
                source.Position = 0;
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(source, cancellationToken).ConfigureAwait(false));
                hashes[input.SourcePath] = hash;
                source.Position = 0;
                var destination = Path.Combine(attemptRoot, input.RelativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous);
                await using (output.ConfigureAwait(false))
                {
                    await source.CopyToAsync(output, cancellationToken).ConfigureAwait(false);
                }
            }

            return hashes;
        }
        finally
        {
            foreach (var stream in streams.Values)
            {
                await stream.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static ImmutableArray<CookSnapshotInput> NormalizeInputs(IReadOnlyList<CookSnapshotInput> inputs)
    {
        var targets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var normalized = ImmutableArray.CreateBuilder<CookSnapshotInput>(inputs.Count);
        foreach (var input in inputs)
        {
            var relative = input.RelativePath.Replace('\\', '/');
            if (!Path.IsPathFullyQualified(input.SourcePath)
                || Path.IsPathRooted(relative)
                || relative.Split('/').Any(static part => part is ".." or "." or ""
                    || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
                    || part.EndsWith('.') || part.EndsWith(' '))
                || !targets.Add(relative)
                || input.DiscoveryHash.Length != SHA256.HashSizeInBytes * 2
                || !input.DiscoveryHash.All(Uri.IsHexDigit))
            {
                throw new ArgumentException("Snapshot inputs require absolute sources, unique contained relative paths, and discovery hashes.", nameof(inputs));
            }

            normalized.Add(input with
            {
                SourcePath = Path.GetFullPath(input.SourcePath),
                RelativePath = relative,
                DiscoveryHash = input.DiscoveryHash.ToUpperInvariant(),
            });
        }

        return normalized.MoveToImmutable();
    }

    private static string ComputeIdentity(string buildFingerprint, ImmutableArray<CookSnapshotInput> inputs)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            Build = buildFingerprint,
            Inputs = inputs.OrderBy(static input => input.RelativePath, StringComparer.Ordinal)
                .Select(static input => new { Uri = input.AssetUri?.AbsoluteUri, input.RelativePath, Hash = input.DiscoveryHash }),
        });
        return Convert.ToHexString(SHA256.HashData(bytes));
    }
}
