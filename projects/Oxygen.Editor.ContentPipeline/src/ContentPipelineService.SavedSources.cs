// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads acknowledged saved bytes without retaining live authoring objects.</summary>
public sealed partial class ContentPipelineService
{
    private async Task<byte[]> ReadSavedSourceAsync(string sourcePath, CancellationToken cancellationToken)
    {
        using var reads = await cookDocuments.AcquireAsync([sourcePath], cancellationToken).ConfigureAwait(false);
        var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
        if (dirty.Length != 0)
        {
            throw new CookInputsNeedSaveException(dirty);
        }

        // Exclude external writes and replacement until the complete saved file is in memory.
        var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = source.ConfigureAwait(false);
        var buffer = new MemoryStream();
        await using var bufferLifetime = buffer.ConfigureAwait(false);
        await source.CopyToAsync(buffer, cancellationToken).ConfigureAwait(false);
        var bytes = buffer.ToArray();
        var hash = Convert.ToHexString(SHA256.HashData(bytes));
        foreach (var document in reads.Documents)
        {
            if (!string.Equals(hash, document.SavedContentHash, StringComparison.OrdinalIgnoreCase))
            {
                throw new IOException($"'{document.DisplayName}' changed outside the editor. Reload it before cooking. Source: {sourcePath}");
            }
        }

        return bytes;
    }
}
