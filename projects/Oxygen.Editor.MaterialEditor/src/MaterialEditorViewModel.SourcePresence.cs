// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Dispatching;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.MaterialEditor;

/// <content>Follows the material document's file and state.</content>
public sealed partial class MaterialEditorViewModel
{
    private IDisposable? sourceChanges;

    // File watching reports the material's file through the catalog: changed, removed, renamed away, or added back.
    private void WatchSourcePresence(IAssetCatalog? catalog)
    {
        if (catalog is null || DispatcherQueue.GetForCurrentThread() is not { } dispatcher)
        {
            return;
        }

        this.sourceChanges = catalog.Changes.Subscribe(change => _ = dispatcher.TryEnqueue(() => this.OnSourceChanged(change)));
    }

    private void StopWatchingSourcePresence()
    {
        this.sourceChanges?.Dispose();
        this.sourceChanges = null;
    }

    private void OnSourceChanged(AssetChange change)
    {
        // Catalog changes name the source file; the document is the asset that file stands for.
        var asset = this.document.File.Asset;
        if (!this.isDisposed && (AssetFileTypes.ToIdentity(change.Identity) == asset
            || (change.PreviousIdentity is { } previous && AssetFileTypes.ToIdentity(previous) == asset)))
        {
            _ = this.CheckSourceAsync();
        }
    }

    private async Task CheckSourceAsync()
    {
        try
        {
            _ = await this.document.CheckFileAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // The file is unreadable right now; the next change checks again.
        }
    }

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
        this.HasSaveConflict = false;
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
