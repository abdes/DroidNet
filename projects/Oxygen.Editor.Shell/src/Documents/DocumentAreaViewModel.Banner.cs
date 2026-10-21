// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The banner of the active document: its file is missing on disk, or it changed on disk while the document has
///     unsaved changes.
/// </summary>
public sealed partial class DocumentAreaViewModel
{
    private Document? observedDocument;

    /// <summary>
    ///     Gets a value indicating whether the active document's file was deleted or moved outside the editor.
    /// </summary>
    [ObservableProperty]
    public partial bool IsActiveDocumentMissingOnDisk { get; private set; }

    /// <summary>
    ///     Gets what the missing banner says.
    /// </summary>
    [ObservableProperty]
    public partial string MissingOnDiskNotice { get; private set; } = string.Empty;

    /// <summary>
    ///     Gets a value indicating whether the active document's file changed outside the editor while the document has
    ///     unsaved changes, so that saving would overwrite the outside change.
    /// </summary>
    [ObservableProperty]
    public partial bool IsActiveDocumentChangedOnDisk { get; private set; }

    /// <summary>
    ///     Gets what the conflict banner says, including the outcome of the last action taken from it.
    /// </summary>
    [ObservableProperty]
    public partial string ChangedOnDiskNotice { get; private set; } = string.Empty;

    private void OnActiveDocumentChanged(object? sender, EventArgs e) => this.ObserveActiveDocument(this.area.ActiveDocument);

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
            document?.StateChanged += this.OnActiveDocumentStateChanged;
        }

        this.RefreshBanner(outcome: null);
    }

    private void OnActiveDocumentStateChanged(object? sender, EventArgs e) => this.RefreshBanner(outcome: null);

    private void RefreshBanner(string? outcome)
    {
        var document = this.observedDocument;
        this.IsActiveDocumentMissingOnDisk = document?.IsMissingOnDisk == true;
        this.MissingOnDiskNotice = document is { IsMissingOnDisk: true }
            ? $"“{document.Title}” was deleted or moved outside the editor. Save writes it again at its location; Close discards it."
            : string.Empty;
        this.IsActiveDocumentChangedOnDisk = document?.IsChangedOnDisk == true;
        this.ChangedOnDiskNotice = document is { IsChangedOnDisk: true }
            ? $"“{document.Title}” changed outside the editor, and your changes are not saved. Saving would overwrite the file."
                + (outcome is null ? string.Empty : " " + outcome)
            : string.Empty;
        this.SaveActiveDocumentCommand.NotifyCanExecuteChanged();
    }

    [RelayCommand(CanExecute = nameof(HasActiveDocument))]
    private async Task SaveActiveDocument()
    {
        if (this.area.ActiveDocument is { } active)
        {
            _ = await this.documents.SaveAsync(active).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task CloseActiveDocument()
    {
        if (this.area.ActiveDocument is { } active)
        {
            _ = await this.area.CloseAsync(active).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task ReloadActiveDocument()
    {
        if (this.area.ActiveDocument is { } active && await this.confirmations.ConfirmReloadAsync(active).ConfigureAwait(true))
        {
            var result = await this.documents.ReloadAsync(active).ConfigureAwait(true);
            this.ShowOutcome(active, result.Message);
        }
    }

    [RelayCommand]
    private async Task KeepMine()
    {
        if (this.area.ActiveDocument is { } active)
        {
            await this.documents.KeepMineAsync(active).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task SaveActiveDocumentCopy()
    {
        if (this.area.ActiveDocument is { } active && await this.confirmations.AskCopyNameAsync(active).ConfigureAwait(true) is { } name)
        {
            var result = await this.documents.SaveCopyAsync(active, name).ConfigureAwait(true);
            this.ShowOutcome(active, result.Message);
        }
    }

    private bool HasActiveDocument() => this.area.ActiveDocument is not null;

    private void ShowOutcome(Document document, string message)
    {
        this.LogConflictAction(document.Title, message);
        if (ReferenceEquals(document, this.observedDocument))
        {
            this.RefreshBanner(message);
        }
    }
}
