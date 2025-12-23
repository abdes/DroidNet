// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Minimal asset listing record returned by the catalog.
/// </summary>
/// <remarks>
/// Catalog records are intentionally lightweight. Consumers that need the full asset metadata
/// should load the asset via <see cref="IAssetService"/>.
/// </remarks>
public sealed record AssetRecord(Uri Uri)
{
    /// <summary>
    /// Gets the asset name derived from the URI path.
    /// </summary>
    public string Name => Path.GetFileNameWithoutExtension(this.Uri.AbsolutePath);
}
