// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
/// Add to scene (scene instances 6.1, 8.6): places the selected model or scene in the open scene, as the scene
/// toolbar's <em>Add</em> menu does, and shows the scene.
/// </summary>
public partial class AssetsViewModel
{
    /// <summary>Gets what Add to scene does, or why it is unavailable.</summary>
    public string AddToSceneToolTip => this.GetAddToSceneBlocker()
        ?? "Place the selected model or scene in the open scene, in front of its viewport, and select it.";

    // A model counts once an import names it; a model's own scene product is picked as the model.
    private static bool IsInstanceSource(AssetEntry asset)
        => AssetRule.For(AssetKind.Model).Accepts(asset) || AssetRule.For(AssetKind.Scene).Accepts(asset);

    private Document? FindOpenScene() => documents?.Documents.FirstOrDefault(static document => document.Content is SceneContent);

    private string? GetAddToSceneBlocker()
    {
        if (sceneViews is null || documents is null)
        {
            return UnavailableReason;
        }

        if (this.SelectedAssets is not [{ } asset] || !IsInstanceSource(asset))
        {
            return "Select one imported model or one scene.";
        }

        if (this.FindOpenScene() is not { } scene)
        {
            return "Open a scene to place it in.";
        }

        return asset.Identity == AssetFileTypes.ToIdentity(scene.File.Asset) ? "A scene cannot be placed in itself." : null;
    }

    private bool CanAddToScene() => this.GetAddToSceneBlocker() is null;

    [RelayCommand(CanExecute = nameof(CanAddToScene))]
    private async Task AddToSceneAsync()
    {
        if (!this.CanAddToScene() || documents is not { } area || sceneViews is not { } views || this.FindOpenScene() is not { } scene)
        {
            return;
        }

        // The scene is shown first: the instance goes in front of its active viewport, where the user sees it placed.
        var source = this.SelectedAssets[0].Identity;
        _ = await area.ShowAsync(scene).ConfigureAwait(true);
        _ = await views.AddInstanceAsync(scene.Id, source).ConfigureAwait(true);
    }

    private void OnDocumentsChanged(object? sender, EventArgs e) => this.NotifyAssetActions();
}
