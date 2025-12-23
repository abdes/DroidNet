// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Import;

/// <summary>
/// Provides stable <see cref="AssetKey"/> ownership for virtual paths.
/// </summary>
public interface IAssetIdentityPolicy
{
    /// <summary>
    /// Gets an existing key for the virtual path or creates a new one.
    /// </summary>
    /// <param name="virtualPath">The canonical virtual path.</param>
    /// <param name="assetType">Logical asset type (for example <c>Material</c>).</param>
    /// <returns>A stable asset key.</returns>
    public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType);
}
