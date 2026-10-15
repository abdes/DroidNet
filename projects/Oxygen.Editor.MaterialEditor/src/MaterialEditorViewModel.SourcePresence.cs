// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Marks the open material when its file is deleted or moved outside the editor, and clears the mark when it returns.</summary>
public sealed partial class MaterialEditorViewModel
{
    private async Task RefreshSourcePresenceAsync()
    {
        if (this.isDisposed || this.document is not { } current)
        {
            return;
        }

        bool missing;
        try
        {
            missing = await this.documentService.RefreshSourcePresenceAsync(current.DocumentId, CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or KeyNotFoundException)
        {
            // The file is unreadable or the document closed meanwhile; the next change checks again.
            return;
        }

        if (this.isDisposed || missing == this.metadata.IsMissingOnDisk)
        {
            return;
        }

        this.metadata.IsMissingOnDisk = missing;
        this.IsDirty = this.metadata.IsDirty;
        if (this.documents is { } host)
        {
            _ = await host.UpdateMetadataAsync(this.windowId, this.metadata.DocumentId, this.metadata).ConfigureAwait(true);
        }
    }
}
