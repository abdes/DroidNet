// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <content>The banner of an active document whose file is missing on disk.</content>
public partial class DocumentHostViewModel
{
    private Document? observedDocument;

    [ObservableProperty]
    public partial bool IsActiveDocumentMissingOnDisk { get; private set; }

    [ObservableProperty]
    public partial string MissingOnDiskNotice { get; private set; } = string.Empty;

    [RelayCommand]
    private async Task CloseActiveDocument()
    {
        if (this.documentArea.ActiveDocument is { } active)
        {
            _ = await this.documentArea.CloseAsync(active).ConfigureAwait(true);
        }
    }

    private void OnActiveDocumentChanged(object? sender, EventArgs e) => this.ObserveActiveDocument(this.documentArea.ActiveDocument);

    // The banner follows the active document's state until another document becomes active.
    private void ObserveActiveDocument(Document? document)
    {
        if (!ReferenceEquals(this.observedDocument, document))
        {
            if (this.observedDocument is { } previous)
            {
                previous.StateChanged -= this.OnActiveDocumentStateChanged;
            }

            this.observedDocument = document;
            if (document is not null)
            {
                document.StateChanged += this.OnActiveDocumentStateChanged;
            }
        }

        this.RefreshMissingOnDiskNotice();
    }

    private void OnActiveDocumentStateChanged(object? sender, EventArgs e) => this.RefreshMissingOnDiskNotice();

    private void RefreshMissingOnDiskNotice()
    {
        var document = this.observedDocument;
        this.IsActiveDocumentMissingOnDisk = document?.IsMissingOnDisk == true;
        this.MissingOnDiskNotice = document is { IsMissingOnDisk: true }
            ? $"“{document.Title}” was deleted or moved outside the editor. Save writes it again at its location; Close discards it."
            : string.Empty;
    }
}
