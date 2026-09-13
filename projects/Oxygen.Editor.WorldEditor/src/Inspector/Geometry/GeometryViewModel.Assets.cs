// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Projects shared identities and availability into stable geometry choices.</summary>
public sealed partial class GeometryViewModel
{
    private IReadOnlyList<ContentBrowserAssetItem> latestAssets = [];

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
            await this.assetProvider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            Debug.WriteLine($"[GeometryViewModel] Shared asset refresh failed: {exception}");
        }
    }

    private void ApplySharedAssets(IReadOnlyList<ContentBrowserAssetItem> assets)
    {
        if (this.disposed)
        {
            return;
        }

        this.latestAssets = assets;
        var geometries = assets.Where(static asset => asset.Kind == AssetKind.Geometry && !asset.IsBuiltin)
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase).ToArray();
        var wanted = geometries.Select(static asset => asset.IdentityUri.AbsoluteUri).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var removed in this.contentItemsByKey.Keys.Where(key => !wanted.Contains(key)).ToArray())
        {
            _ = this.contentItems.Remove(this.contentItemsByKey[removed]);
            _ = this.contentItemsByKey.Remove(removed);
        }

        for (var index = 0; index < geometries.Length; index++)
        {
            var asset = geometries[index];
            var item = new AssetPickerItem(
                asset.DisplayName,
                asset.IdentityUri,
                "Geometry · " + asset.PrimaryBadge,
                asset.DisplayPath,
                AssetPickerGroup.Content,
                asset.IsSelectable,
                "\uF158");
            if (this.contentItemsByKey.TryGetValue(asset.IdentityUri.AbsoluteUri, out var row))
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
                this.contentItemsByKey.Add(asset.IdentityUri.AbsoluteUri, row);
                this.contentItems.Insert(index, row);
            }
        }

        this.ApplyBuiltinCatalog();
        if (!this.IsMixed && Uri.TryCreate(this.SelectedAssetUriString, UriKind.Absolute, out var selected)
            && assets.FirstOrDefault(asset => asset.IdentityUri == selected || asset.CookedUri == selected) is { } current)
        {
            this.SelectedAssetName = current.DisplayName;
        }
    }

    private string ResolveGeometryDisplayName(Uri uri)
        => this.latestAssets.FirstOrDefault(asset => asset.IdentityUri == uri || asset.CookedUri == uri)?.DisplayName
            ?? ExtractNameFromUriString(uri.ToString());

    private string BuiltinAvailability(Uri uri)
    {
        var asset = this.latestAssets.FirstOrDefault(item => item.IdentityUri == uri);
        return asset?.RuntimeAvailability switch
        {
            AssetRuntimeAvailability.Unavailable or AssetRuntimeAvailability.NotMounted => " · Preview unavailable",
            AssetRuntimeAvailability.Updating => " · Updating preview",
            AssetRuntimeAvailability.Failed => " · Preview issue",
            _ => string.Empty,
        };
    }
}
