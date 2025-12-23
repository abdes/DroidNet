// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Storage;

namespace Oxygen.Assets.Validation.LooseCooked.V1;

/// <summary>
/// Validation helpers for runtime-compatible loose cooked v1 containers.
/// </summary>
public static class LooseCookedIndexValidator
{
    /// <summary>
    /// Validates a v1 container index and (optionally) verifies referenced file hashes/sizes.
    /// </summary>
    /// <param name="storage">Storage provider used to read the cooked files.</param>
    /// <param name="cookedRootFolderPath">Cooked root folder containing the index and payload files.</param>
    /// <param name="verifyFileRecords">When true, verifies all file records exist, size matches, and SHA-256 matches.</param>
    /// <param name="verifyAssetDescriptors">When true, verifies all asset descriptors exist, size matches, and SHA-256 matches.</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    /// <returns>A list of validation issues (empty means valid).</returns>
    public static async Task<IReadOnlyList<LooseCookedValidationIssue>> ValidateAsync(
        IStorageProvider storage,
        string cookedRootFolderPath,
        bool verifyFileRecords,
        bool verifyAssetDescriptors,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentException.ThrowIfNullOrEmpty(cookedRootFolderPath);

        var root = storage.Normalize(cookedRootFolderPath);

        var (index, openIssues) = await TryLoadIndexAsync(storage, root, cancellationToken).ConfigureAwait(false);
        if (openIssues.Count > 0)
        {
            return openIssues;
        }

        var issues = new List<LooseCookedValidationIssue>();
        if (verifyAssetDescriptors)
        {
            await ValidateAssetDescriptorsAsync(storage, root, index, issues, cancellationToken).ConfigureAwait(false);
        }

        if (verifyFileRecords)
        {
            await ValidateFileRecordsAsync(storage, root, index, issues, cancellationToken).ConfigureAwait(false);
        }

        return issues;
    }

    private static async Task<(Document index, List<LooseCookedValidationIssue> issues)> TryLoadIndexAsync(
        IStorageProvider storage,
        string cookedRoot,
        CancellationToken cancellationToken)
    {
        var indexPath = storage.NormalizeRelativeTo(cookedRoot, "container.index.bin");

        IDocument indexDoc;
        try
        {
            indexDoc = await storage.GetDocumentFromPathAsync(indexPath, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is InvalidPathException or StorageException)
        {
            return (new Document(0, IndexFeatures.None, [], []),
                [new LooseCookedValidationIssue("index.open", $"Failed to locate index: {ex.Message}")]);
        }

        if (!await indexDoc.ExistsAsync().ConfigureAwait(false))
        {
            return (new Document(0, IndexFeatures.None, [], []),
                [new LooseCookedValidationIssue("index.missing", "container.index.bin is missing.")]);
        }

        try
        {
            var stream = await indexDoc.OpenReadAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                var index = LooseCookedIndex.Read(stream);
                return (index, []);
            }
            finally
            {
                await stream.DisposeAsync().ConfigureAwait(false);
            }
        }
        catch (Exception ex) when (ex is InvalidDataException or NotSupportedException or StorageException)
        {
            return (new Document(0, IndexFeatures.None, [], []),
                [new LooseCookedValidationIssue("index.parse", $"Failed to parse container.index.bin: {ex.Message}")]);
        }
    }

    private static async Task ValidateAssetDescriptorsAsync(
        IStorageProvider storage,
        string cookedRoot,
        Document index,
        List<LooseCookedValidationIssue> issues,
        CancellationToken cancellationToken)
    {
        foreach (var asset in index.Assets)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (string.IsNullOrEmpty(asset.DescriptorRelativePath))
            {
                issues.Add(new LooseCookedValidationIssue("asset.descriptorPath", "Asset descriptor path is missing."));
                continue;
            }

            var relative = asset.DescriptorRelativePath.Replace('/', Path.DirectorySeparatorChar);
            var descriptorPath = storage.NormalizeRelativeTo(cookedRoot, relative);
            var descriptor = await storage.GetDocumentFromPathAsync(descriptorPath, cancellationToken).ConfigureAwait(false);

            if (!await descriptor.ExistsAsync().ConfigureAwait(false))
            {
                issues.Add(new LooseCookedValidationIssue("asset.descriptorMissing", $"Descriptor missing: {asset.DescriptorRelativePath}"));
                continue;
            }

            try
            {
                var s = await descriptor.OpenReadAsync(cancellationToken).ConfigureAwait(false);
                try
                {
                    var (length, sha) = await ComputeSha256Async(s, cancellationToken).ConfigureAwait(false);
                    AddSizeAndHashIssues(
                        expectedSize: asset.DescriptorSize,
                        expectedSha256: asset.DescriptorSha256.Span,
                        actualSize: (ulong)length,
                        actualSha256: sha,
                        sizeCode: "asset.descriptorSize",
                        shaCode: "asset.descriptorSha256",
                        label: asset.DescriptorRelativePath,
                        issues);
                }
                finally
                {
                    await s.DisposeAsync().ConfigureAwait(false);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StorageException or ItemNotFoundException)
            {
                issues.Add(new LooseCookedValidationIssue(
                    "asset.descriptorRead",
                    $"Failed to read descriptor {asset.DescriptorRelativePath}: {ex.Message}"));
            }
        }
    }

    private static async Task ValidateFileRecordsAsync(
        IStorageProvider storage,
        string cookedRoot,
        Document index,
        List<LooseCookedValidationIssue> issues,
        CancellationToken cancellationToken)
    {
        foreach (var file in index.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var rel = file.RelativePath;
            if (string.IsNullOrEmpty(rel))
            {
                issues.Add(new LooseCookedValidationIssue("file.path", "File record path is missing."));
                continue;
            }

            var relative = rel.Replace('/', Path.DirectorySeparatorChar);
            var path = storage.NormalizeRelativeTo(cookedRoot, relative);
            var doc = await storage.GetDocumentFromPathAsync(path, cancellationToken).ConfigureAwait(false);

            if (!await doc.ExistsAsync().ConfigureAwait(false))
            {
                issues.Add(new LooseCookedValidationIssue("file.missing", $"File missing: {rel}"));
                continue;
            }

            try
            {
                var s = await doc.OpenReadAsync(cancellationToken).ConfigureAwait(false);
                try
                {
                    var (length, sha) = await ComputeSha256Async(s, cancellationToken).ConfigureAwait(false);
                    AddSizeAndHashIssues(
                        expectedSize: file.Size,
                        expectedSha256: file.Sha256.Span,
                        actualSize: (ulong)length,
                        actualSha256: sha,
                        sizeCode: "file.size",
                        shaCode: "file.sha256",
                        label: rel,
                        issues);
                }
                finally
                {
                    await s.DisposeAsync().ConfigureAwait(false);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StorageException or ItemNotFoundException)
            {
                issues.Add(new LooseCookedValidationIssue(
                    "file.read",
                    $"Failed to read file {rel}: {ex.Message}"));
            }
        }
    }

    private static void AddSizeAndHashIssues(
        ulong expectedSize,
        ReadOnlySpan<byte> expectedSha256,
        ulong actualSize,
        ReadOnlySpan<byte> actualSha256,
        string sizeCode,
        string shaCode,
        string label,
        List<LooseCookedValidationIssue> issues)
    {
        if (actualSize != expectedSize)
        {
            issues.Add(new LooseCookedValidationIssue(
                sizeCode,
                $"Size mismatch for {label}: expected {expectedSize}, got {actualSize}."));
        }

        if (!actualSha256.SequenceEqual(expectedSha256))
        {
            issues.Add(new LooseCookedValidationIssue(
                shaCode,
                $"SHA-256 mismatch for {label}."));
        }
    }

    private static async Task<(long length, byte[] sha256)> ComputeSha256Async(Stream stream, CancellationToken cancellationToken)
    {
        using var hasher = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);

        var buffer = new byte[64 * 1024];
        long total = 0;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var read = await stream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (read <= 0)
            {
                break;
            }

            total += read;
            hasher.AppendData(buffer, 0, read);
        }

        return (total, hasher.GetHashAndReset());
    }
}
