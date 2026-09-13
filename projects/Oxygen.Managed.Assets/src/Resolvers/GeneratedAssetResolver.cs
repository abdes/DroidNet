// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Resolvers;

/// <summary>
/// Resolves assets from the "Engine" mount point, specifically those under the "Generated" path.
/// </summary>
/// <remarks>
/// <para>
/// Engine-provided assets are registered at construction time and cannot be modified at runtime,
/// ensuring thread safety without locking.
/// </para>
/// </remarks>
public sealed class GeneratedAssetResolver : IAssetResolver
{
    private const string MountPoint = AssetUris.EngineMountPoint;

    private readonly FrozenDictionary<Uri, Asset> assets;

    /// <summary>Initializes a new instance of the <see cref="GeneratedAssetResolver"/> class from engine metadata.</summary>
    /// <param name="assets">The assets to resolve, retaining their authored identities.</param>
    public GeneratedAssetResolver(IEnumerable<Asset> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        this.assets = assets.ToFrozenDictionary(static asset => asset.Uri);
    }

    /// <inheritdoc/>
    public bool CanResolve(string mountPoint)
        => string.Equals(mountPoint, MountPoint, StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc/>
    public Task<Asset?> ResolveAsync(Uri uri)
    {
        var result = this.assets.GetValueOrDefault(uri);
        return Task.FromResult(result);
    }
}
