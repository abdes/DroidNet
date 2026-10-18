// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Projects shared identities and availability into stable geometry choices.</summary>
public sealed partial class GeometryViewModel
{
    private IReadOnlyList<AssetEntry> latestAssets = [];

    private void StartAssetCatalogSubscription()
    {
        this.assetChangesSubscription = this.assetProvider.Items.Subscribe(
            items => this.DispatchOnUi(() => this.ApplySharedAssets(items)),
            exception => Debug.WriteLine($"[GeometryViewModel] Shared asset status failed: {exception}"));
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
            Debug.WriteLine($"[GeometryViewModel] Shared asset refresh failed: {exception}");
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
        var geometries = AssetRule.For(AssetKind.Geometry).Apply(assets, selectedGeometry);
        var wanted = geometries.Select(static asset => asset.Identity).ToHashSet();
        foreach (var removed in this.contentItemsByKey.Keys.Where(key => !wanted.Contains(key)).ToArray())
        {
            _ = this.contentItems.Remove(this.contentItemsByKey[removed]);
            _ = this.contentItemsByKey.Remove(removed);
        }

        for (var index = 0; index < geometries.Count; index++)
        {
            var asset = geometries[index];
            var item = new AssetPickerItem(
                asset.DisplayName,
                asset.Identity,
                "Geometry · " + asset.PrimaryBadge,
                asset.DisplayPath,
                AssetPickerGroup.Content,
                asset.IsSelectable,
                "\uF158");
            if (this.contentItemsByKey.TryGetValue(asset.Identity, out var row))
            {
                row.Update(item);
                if (this.contentItems.IndexOf(row) != index)
                {
                    this.contentItems.Move(this.contentItems.IndexOf(row), index);
                }
            }
            else
            {
                row = new(item);
                this.contentItemsByKey.Add(asset.Identity, row);
                this.contentItems.Insert(index, row);
            }
        }

        this.ApplyBuiltinCatalog();
        if (previousGeometry?.CookedMetadata != currentGeometry?.CookedMetadata
            || previousGeometry?.EffectiveCookedSource != currentGeometry?.EffectiveCookedSource
            || previousGeometry?.CookStatus?.Freshness != currentGeometry?.CookStatus?.Freshness
            || previousGeometry?.CookStatus?.HasAvailableOutput != currentGeometry?.CookStatus?.HasAvailableOutput)
        {
            _ = this.RefreshMaterialSlotsAsync();
        }

        if (!this.IsMixed && this.SelectedGeometry is { } selected && assets.Find(selected) is { } current)
        {
            this.SelectedAssetName = current.DisplayName;
        }
    }

    private string ResolveGeometryDisplayName(AssetPath geometry)
        => this.latestAssets.Find(geometry)?.DisplayName ?? geometry.Name;

    private string BuiltinAvailability(AssetPath uri)
    {
        var asset = this.latestAssets.FirstOrDefault(item => item.Identity == uri);
        return asset?.RuntimeAvailability switch
        {
            AssetRuntimeAvailability.Unavailable or AssetRuntimeAvailability.NotMounted => " · Preview unavailable",
            AssetRuntimeAvailability.Updating => " · Updating preview",
            AssetRuntimeAvailability.Failed => " · Preview issue",
            _ => string.Empty,
        };
    }
}
