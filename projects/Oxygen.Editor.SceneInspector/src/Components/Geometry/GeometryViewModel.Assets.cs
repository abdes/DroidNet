// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>Projects shared identities and availability into stable geometry choices.</summary>
public sealed partial class GeometryViewModel
{
    private IReadOnlyList<AssetEntry> latestAssets = [];

    private void StartAssetCatalogSubscription()
    {
        this.assetChangesSubscription = this.assetProvider.Items.Subscribe(
            items => this.DispatchOnUi(() => this.ApplySharedAssets(items)),
            this.LogAssetFeedFailed);
        _ = this.RefreshSharedAssetsAsync();
    }

    private async Task RefreshSharedAssetsAsync()
    {
        try
        {
            await this.assetProvider.RefreshAsync().ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            this.LogAssetRefreshFailed(exception);
        }
    }

    private void ApplySharedAssets(IReadOnlyList<AssetEntry> assets)
    {
        if (this.disposed)
        {
            return;
        }

        var selectedGeometry = this.SelectedGeometry;
        var previousGeometry = selectedGeometry is null ? null : this.latestAssets.Find(selectedGeometry);
        var currentGeometry = selectedGeometry is null ? null : assets.Find(selectedGeometry);
        this.latestAssets = assets;
        this.GeometryChoices.Update(AssetRule.For(AssetKind.Geometry).Apply(assets, selectedGeometry));
        if (previousGeometry?.CookedMetadata != currentGeometry?.CookedMetadata
            || previousGeometry?.EffectiveCookedSource != currentGeometry?.EffectiveCookedSource
            || previousGeometry?.CookStatus?.Freshness != currentGeometry?.CookStatus?.Freshness
            || previousGeometry?.CookStatus?.HasAvailableOutput != currentGeometry?.CookStatus?.HasAvailableOutput)
        {
            _ = this.RefreshSurfacesAsync();
        }

        if (!this.IsMixed && this.SelectedGeometry is { } selected && assets.Find(selected) is { } current)
        {
            this.SelectedAssetName = current.DisplayName;
        }
    }

    private string ResolveGeometryDisplayName(AssetPath geometry)
        => this.latestAssets.Find(geometry)?.DisplayName ?? geometry.Name;
}
