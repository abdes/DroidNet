// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Tells the user when the active document's file was deleted or moved outside the editor, and offers the choice.</summary>
public partial class DocumentHostViewModel
{
    private Guid? activeDocumentId;

    /// <summary>Gets a value indicating whether the active document's file is missing on disk.</summary>
    [ObservableProperty]
    public partial bool IsActiveDocumentMissingOnDisk { get; private set; }

    /// <summary>Gets the explanation shown while the active document's file is missing on disk.</summary>
    [ObservableProperty]
    public partial string MissingOnDiskNotice { get; private set; } = string.Empty;

    [RelayCommand]
    private async Task CloseActiveDocument()
    {
        if (this.activeDocumentId is { } id)
        {
            _ = await this.DocumentService.CloseDocumentAsync(this.windowId, id).ConfigureAwait(true);
        }
    }

    private void OnDocumentMetadataChanged(object? sender, DocumentMetadataChangedEventArgs e)
    {
        if (e.WindowId == this.windowId && e.NewMetadata.DocumentId == this.activeDocumentId)
        {
            this.RefreshMissingOnDiskNotice();
        }
    }

    private void SetActiveDocument(Guid? documentId)
    {
        this.activeDocumentId = documentId;
        this.RefreshMissingOnDiskNotice();
    }

    private void RefreshMissingOnDiskNotice()
    {
        var metadata = this.activeDocumentId is { } id
            ? this.DocumentService.GetOpenDocuments(this.windowId).FirstOrDefault(document => document.DocumentId == id) as BaseDocumentMetadata
            : null;
        this.IsActiveDocumentMissingOnDisk = metadata?.IsMissingOnDisk == true;
        this.MissingOnDiskNotice = metadata is { IsMissingOnDisk: true }
            ? $"“{metadata.Title}” was deleted or moved outside the editor. Save writes it again at its location; Close discards it."
            : string.Empty;
    }
}
