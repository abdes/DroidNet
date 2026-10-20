// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <content>Follows the material document's state and content.</content>
public sealed partial class MaterialEditorViewModel
{
    // Dirty, missing and the file's name and place follow the document.
    private void OnDocumentStateChanged(object? sender, EventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        this.IsDirty = this.document.IsDirty;
        this.DisplayName = this.document.Title;
        this.MaterialUriText = this.document.File.Asset.ToUriString();
        this.RefreshHistoryCommands();
    }

    // A reload replaced the content: the editor shows the file's values, and its history was cleared with it.
    private void OnDocumentContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        this.ObserveContent();
        this.ReadFromContent();
    }

    private void ObserveContent() => this.ObserveContent(this.document.Content as MaterialContent);

    private void ObserveContent(MaterialContent? content)
    {
        if (ReferenceEquals(this.observedContent, content))
        {
            return;
        }

        if (this.observedContent is { } previous)
        {
            previous.Material.Changed -= this.OnContentSourceChanged;
        }

        this.observedContent = content;
        content?.Material.Changed += this.OnContentSourceChanged;
    }

    // Undo, redo and relocations change the source behind the controls; the controls show the new values.
    private void OnContentSourceChanged(object? sender, EventArgs args)
    {
        if (!this.isDisposed && !this.isApplyingOwnEdit && !this.isLoading)
        {
            this.ReadFromContent();
        }
    }
}
