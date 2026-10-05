// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Reduces primitive catalog records into shared Content Browser row items.
/// </summary>
public interface IAssetIdentityReducer
{
    public IReadOnlyList<ContentBrowserAssetItem> Reduce(
        IReadOnlyList<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope,
        AssetBrowserFilter filter);

    public ContentBrowserAssetItem CreateMissing(Uri uri);
}
