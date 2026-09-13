// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads acknowledged saved bytes without retaining live authoring objects.</summary>
internal static class CookSavedSourceReader
{
    /// <summary>Reads one source while excluding saves and unacknowledged external changes.</summary>
    /// <param name="documents">Registered source owners.</param>
    /// <param name="sourcePath">The absolute source path.</param>
    /// <param name="cancellationToken">Cancels the protected read.</param>
    /// <param name="allowUnsavedDocuments">Reads acknowledged saved bytes for status inspection while an owner has newer unsaved edits.</param>
    /// <returns>The saved bytes released from their source read lease.</returns>
    public static async Task<byte[]> ReadAsync(ICookDocumentRegistry documents, string sourcePath, CancellationToken cancellationToken, bool allowUnsavedDocuments = false)
    {
        using var reads = await documents.AcquireAsync([sourcePath], cancellationToken).ConfigureAwait(false);
        var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
        if (!allowUnsavedDocuments && dirty.Length != 0)
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

    /// <summary>Checks presence without treating inaccessible input as an absent optional file.</summary>
    /// <remarks>FileSystemInfo reports missing entries with -1 while preserving errors reading attributes.</remarks>
    /// <param name="path">The input path.</param>
    /// <returns>Whether a filesystem entry exists at the path.</returns>
    public static bool Exists(string path)
        => new FileInfo(path).Attributes != (FileAttributes)(-1);
}
