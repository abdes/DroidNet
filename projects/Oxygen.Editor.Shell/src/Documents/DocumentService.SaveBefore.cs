// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>Saving the unsaved documents an operation that reads saved files needs, after asking.</summary>
public sealed partial class DocumentService
{
    /// <inheritdoc/>
    public async Task<bool> SaveBeforeAsync(
        SaveBeforeOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>> findUnsaved,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(findUnsaved);

        // An edit in progress becomes an edit of its own first, so its document counts as unsaved.
        await this.inputCommitter.CommitAsync().ConfigureAwait(true);
        foreach (var document in this.documents.Documents.Where(static document => document.Content.HasEditsInProgress).ToArray())
        {
            await document.Content.FinishEditsAsync(cancellationToken).ConfigureAwait(true);
        }

        var needed = await findUnsaved(cancellationToken).ConfigureAwait(true);
        if (needed.Count == 0)
        {
            return true;
        }

        if (!await this.confirmations.ConfirmSaveBeforeAsync(needed, operation).ConfigureAwait(true))
        {
            this.LogSaveBeforeCancelled(operation, needed.Count);
            return false;
        }

        foreach (var document in needed.Select(item => this.documents.Find(item.Id)).OfType<Document>().ToArray())
        {
            if (!await this.SaveAsync(document).ConfigureAwait(true))
            {
                this.LogSaveBeforeFailed(operation, document.Title);
                return false;
            }
        }

        return true;
    }
}
