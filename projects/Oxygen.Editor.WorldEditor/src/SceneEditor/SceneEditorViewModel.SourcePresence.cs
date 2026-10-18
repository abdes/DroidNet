// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Dispatching;
using Oxygen.Editor.World.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Tells the scene document when its file changes on disk: a scene without edits reloads quietly, one with edits is
/// marked changed on disk, and one whose file is gone stays open, marked missing.
/// </summary>
public partial class SceneEditorViewModel
{
    private IDisposable? sourceChanges;

    // File watching reports the scene's file through the catalog: changed, removed, renamed away, or added back.
    private void WatchSourcePresence(IAssetCatalog? catalog)
    {
        if (catalog is null || DispatcherQueue.GetForCurrentThread() is not { } dispatcher)
        {
            return;
        }

        this.sourceChanges = catalog.Changes.Subscribe(change => _ = dispatcher.TryEnqueue(() => this.OnSourceChanged(change)));
    }

    private void OnSourceChanged(AssetChange change)
    {
        if (this.isDisposed || this.scene is null)
        {
            return;
        }

        // Catalog changes name the source file; the document is the asset that file stands for.
        var asset = this.Document.File.Asset;
        if (AssetFileTypes.ToIdentity(change.Identity) == asset
            || (change.PreviousIdentity is { } previous && AssetFileTypes.ToIdentity(previous) == asset))
        {
            _ = this.CheckSourceAsync();
        }
    }

    private async Task CheckSourceAsync()
    {
        try
        {
            // A quiet reload replaces the scene; with edits in progress the document keeps them and does not reload.
            if (this.scene is not null)
            {
                _ = await this.Document.CheckFileAsync().ConfigureAwait(true);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // The file is unreadable right now; the next change checks again.
        }
    }

    private void StopWatchingSourcePresence()
    {
        this.sourceChanges?.Dispose();
        this.sourceChanges = null;
    }
}
