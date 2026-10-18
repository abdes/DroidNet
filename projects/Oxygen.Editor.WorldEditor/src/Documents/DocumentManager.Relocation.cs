// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Editor-made file changes to open documents. Open documents follow renames and moves in memory as relocation
/// participants; here the tab of a document whose file the editor deleted closes.
/// </summary>
public sealed partial class DocumentManager
{
    private void RegisterRelocationMessages() => this.relocation.FilesChanged += this.OnFilesChanged;

    private void OnFilesChanged(object? sender, AssetFilesChangedEventArgs args) => _ = this.FollowChangedFilesAsync(args);

    private async Task FollowChangedFilesAsync(AssetFilesChangedEventArgs change)
    {
        if (this.disposed)
        {
            return;
        }

        foreach (var document in this.documentArea.Documents)
        {
            var path = document.File.Path;
            if (!change.DeletedFiles.Any(path.IsSameOrInside))
            {
                continue;
            }

            // Deleting an open asset in the editor closes its document; the delete already asked about it.
            if (await this.documentArea.CloseAsync(document, force: true).ConfigureAwait(true))
            {
                this.LogDeletedDocumentClosed(document.Title, path);
            }
            else
            {
                this.LogDeletedDocumentNotClosed(document.Title, path);
            }
        }
    }
}
