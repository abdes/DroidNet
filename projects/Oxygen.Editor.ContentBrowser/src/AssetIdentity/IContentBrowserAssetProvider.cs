// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Shared content-browser row provider over project catalog composition.
/// </summary>
public interface IContentBrowserAssetProvider
{
    IObservable<IReadOnlyList<ContentBrowserAssetItem>> Items { get; }

    Task RefreshAsync(AssetBrowserFilter filter, CancellationToken cancellationToken = default);

    Task<ContentBrowserAssetItem?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default);
}
