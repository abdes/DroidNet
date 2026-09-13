// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Coordinates saved material capture with Save, Reload, and document close.</summary>
public sealed partial class MaterialDocumentService
{
    private async Task<CookDocumentReadLease?> AcquireCookReadAsync(Guid documentId, CancellationToken cancellationToken)
    {
        SemaphoreSlim gate;
        lock (this.sync)
        {
            if (!this.saveGates.TryGetValue(documentId, out gate!))
            {
                return null;
            }
        }

        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var transferred = false;
        try
        {
            lock (this.sync)
            {
                if (!this.documents.TryGetValue(documentId, out var document))
                {
                    return null;
                }

                var state = this.CreateCookDocumentState(document);
                var lease = new CookDocumentReadLease(state, () => gate.Release());
                transferred = true;
                return lease;
            }
        }
        finally
        {
            if (!transferred)
            {
                _ = gate.Release();
            }
        }
    }

    private CookDocumentState CreateCookDocumentState(MaterialDocument document)
        => new(
            document.DocumentId,
            Path.GetFullPath(document.SourcePath),
            document.DisplayName,
            document.Revision,
            document.SavedRevision,
            !SameSource(this.histories[document.DocumentId].SavedSource, document.Source),
            this.fileVersions[document.DocumentId].Sha256);

    private void PublishCookDocumentState(Guid documentId)
    {
        if (this.cookRegistrations.TryGetValue(documentId, out var registration))
        {
            registration.UpdateState(this.CreateCookDocumentState(this.documents[documentId]));
        }
    }
}
