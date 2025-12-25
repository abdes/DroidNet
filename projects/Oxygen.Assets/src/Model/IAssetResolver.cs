// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Model;

/// <summary>
/// Defines a strategy for resolving asset URIs to asset instances.
/// </summary>
/// <remarks>
/// <para>
/// Resolvers are responsible for loading assets from specific sources (e.g., filesystem, PAK files, memory).
/// Each resolver handles one or more mount points (e.g., "Content", "Engine").
/// </para>
/// <para>
/// The <see cref="IAssetService"/> maintains a registry of resolvers and delegates resolution requests
/// based on the URI's mount point.
/// </para>
/// </remarks>
public interface IAssetResolver
{
    /// <summary>
    /// Determines whether this resolver can handle the specified mount point.
    /// </summary>
    /// <param name="mountPoint">The mount point from the asset URI (e.g., "Content", "Engine").</param>
    /// <returns><see langword="true"/> if this resolver can handle the mount point; otherwise, <see langword="false"/>.</returns>
    public bool CanResolve(string mountPoint);

    /// <summary>
    /// Asynchronously resolves an asset URI to an asset instance.
    /// </summary>
    /// <param name="uri">The full asset URI to resolve (e.g., "asset:///Engine/Generated/BasicShapes/Cube").</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the resolved <see cref="Asset"/>,
    /// or <see langword="null"/> if the asset could not be found or loaded.
    /// </returns>
    public Task<Asset?> ResolveAsync(Uri uri);
}
