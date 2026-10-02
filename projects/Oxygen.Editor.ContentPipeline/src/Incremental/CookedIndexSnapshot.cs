// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Index identity and catalog metadata, without certifying payload integrity.</summary>
internal sealed record CookedIndexSnapshot(Document Index, string Fingerprint)
{
    /// <summary>Reads one immutable index opening. No content payload is read.</summary>
    public static async Task<CookedIndexSnapshot> ReadAsync(string root, CancellationToken cancellationToken)
    {
        var file = new FileStream(Path.Combine(root, "container.index.bin"), FileMode.Open,
            FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = file.ConfigureAwait(false);
        using var bytes = new MemoryStream();
        await file.CopyToAsync(bytes, cancellationToken).ConfigureAwait(false);
        bytes.Position = 0;
        var index = LooseCookedIndex.Read(bytes);
        return new(index, Convert.ToHexString(SHA256.HashData(bytes.GetBuffer().AsSpan(0, checked((int)bytes.Length)))));
    }

    /// <summary>Checks the protected member list and sizes, without hashing their contents.</summary>
    public void ValidateMetadata(IReadOnlyList<CookedFileEntry> actual)
    {
        var expected = this.Index.Assets.Select(static asset => (Path: asset.DescriptorRelativePath, Size: asset.DescriptorSize))
            .Concat(this.Index.Files.Select(static file => (Path: file.RelativePath, file.Size)))
            .ToDictionary(static file => file.Path, static file => file.Size, StringComparer.Ordinal);
        foreach (var file in actual.Where(static file => file.RelativePath is not ("container.index.bin" or ".generation.lock")))
        {
            if (!expected.Remove(file.RelativePath, out var size) || file.Size != size)
            {
                throw new InvalidDataException($"Cooked content metadata does not match its index: {file.RelativePath}.");
            }
        }

        if (expected.Count != 0)
        {
            throw new InvalidDataException($"Cooked content is missing an indexed file: {expected.First().Key}.");
        }
    }
}
