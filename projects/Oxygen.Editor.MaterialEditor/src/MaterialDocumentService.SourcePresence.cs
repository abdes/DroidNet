// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Keeps an open material saveable when its file is deleted or moved outside the editor.</summary>
public sealed partial class MaterialDocumentService
{
    private readonly Dictionary<Guid, FileVersion> missingBaselines = [];

    /// <inheritdoc />
    public async Task<bool> RefreshSourcePresenceAsync(Guid documentId, CancellationToken cancellationToken = default)
    {
        MaterialDocument document;
        lock (this.sync)
        {
            document = this.documents[documentId];
        }

        var current = (await this.atomicFiles.ReadAsync(document.SourcePath, cancellationToken).ConfigureAwait(false)).Version;
        lock (this.sync)
        {
            if (!this.fileVersions.TryGetValue(documentId, out var baseline))
            {
                return false;
            }

            if (!current.Exists)
            {
                // A save now creates the file again; a file that reappears meanwhile makes it a conflict.
                if (this.missingBaselines.TryAdd(documentId, baseline))
                {
                    this.fileVersions[documentId] = FileVersion.Missing;
                    this.LogSourceMissing(document.MaterialUri, document.SourcePath);
                }

                return true;
            }

            // Restored unchanged: the document is in step with its file again. Restored with other content: saving
            // still expects the file to be missing, so it reports the ordinary external-change conflict.
            if (this.missingBaselines.Remove(documentId, out var known))
            {
                var unchanged = current == known;
                if (unchanged)
                {
                    this.fileVersions[documentId] = known;
                }

                this.LogSourceRestored(document.MaterialUri, unchanged);
            }

            return false;
        }
    }

    // A save, reload or relocation that wrote or read the file makes its version the baseline again.
    private void ForgetMissingSource(Guid documentId) => _ = this.missingBaselines.Remove(documentId);
}
