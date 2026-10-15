// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Dispatching;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Marks the open scene when its file is deleted or moved outside the editor, and clears the mark when it returns.</summary>
public partial class SceneEditorViewModel
{
    private IDisposable? sourceChanges;

    // File watching reports the scene's file through the catalog: removed, renamed away, or added back.
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
        if (this.isDisposed || this.scene is not { } current)
        {
            return;
        }

        var uri = GetSceneAssetUri(current);
        if (change.Uri == uri || change.PreviousUri == uri)
        {
            _ = this.RefreshSourcePresenceAsync();
        }
    }

    private async Task RefreshSourcePresenceAsync()
    {
        try
        {
            _ = await this.commandService.RefreshSourcePresenceAsync(this.CreateCommandContext()).ConfigureAwait(true);
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
