// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Reduces primitive catalog records into shared Content Browser row items.
/// </summary>
public interface IAssetIdentityReducer
{
    IReadOnlyList<ContentBrowserAssetItem> Reduce(
        IReadOnlyList<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope,
        AssetBrowserFilter filter);

    ContentBrowserAssetItem CreateMissing(Uri uri);
}
