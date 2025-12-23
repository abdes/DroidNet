// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets;

/// <summary>
/// Defines a strategy for resolving asset URIs to asset instances.
/// </summary>
/// <remarks>
/// <para>
/// Resolvers are responsible for loading assets from specific sources (e.g., filesystem, PAK files, memory).
/// Each resolver handles one or more mount point authorities (e.g., "Content", "Engine", "Generated").
/// </para>
/// <para>
/// The <see cref="IAssetService"/> maintains a registry of resolvers and delegates resolution requests
/// based on the URI's authority component.
/// </para>
/// </remarks>
public interface IAssetResolver
{
    /// <summary>
    /// Determines whether this resolver can handle the specified authority.
    /// </summary>
    /// <param name="authority">The mount point authority from the asset URI (e.g., "Content", "Engine", "Generated").</param>
    /// <returns><see langword="true"/> if this resolver can handle the authority; otherwise, <see langword="false"/>.</returns>
    public bool CanResolve(string authority);

    /// <summary>
    /// Asynchronously resolves an asset URI to an asset instance.
    /// </summary>
    /// <param name="uri">The full asset URI to resolve (e.g., "asset://Generated/BasicShapes/Cube").</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the resolved <see cref="Asset"/>,
    /// or <see langword="null"/> if the asset could not be found or loaded.
    /// </returns>
    public Task<Asset?> ResolveAsync(Uri uri);
}
