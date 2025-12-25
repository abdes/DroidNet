// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Assets.Model;
using Oxygen.Core;

namespace Oxygen.Assets.Resolvers;

/// <summary>
/// Resolves assets from the "Engine" mount point, specifically those under the "Generated" path.
/// </summary>
/// <remarks>
/// <para>
/// This resolver maintains a thread-safe, frozen dictionary of built-in assets that are
/// always available in memory. For Phase 4, it provides:
/// <list type="bullet">
/// <item>Basic shape geometries: Cube, Sphere, Plane, Cylinder</item>
/// <item>Default material</item>
/// </list>
/// </para>
/// <para>
/// The assets are registered at construction time and cannot be modified at runtime,
/// ensuring thread safety without locking.
/// </para>
/// </remarks>
public sealed class GeneratedAssetResolver : IAssetResolver
{
    private const string MountPoint = AssetUris.EngineMountPoint;

    private readonly FrozenDictionary<Uri, Asset> assets;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeneratedAssetResolver"/> class
    /// with the default set of built-in assets.
    /// </summary>
    public GeneratedAssetResolver()
    {
        this.assets = BuiltInAssets.Create().ToFrozenDictionary(a => a.Uri);
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
