// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;

namespace Oxygen.Assets.Resolvers;

/// <summary>
/// Resolves assets from the "Generated" mount point, which contains runtime-generated
/// procedural assets (e.g., built-in primitives).
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
    private const string Authority = "Generated";
    private const string BaseUri = "asset://Generated/";

    private readonly FrozenDictionary<Uri, Asset> assets;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeneratedAssetResolver"/> class
    /// with the default set of built-in assets.
    /// </summary>
    public GeneratedAssetResolver()
    {
        this.assets = CreateBuiltInAssets().ToFrozenDictionary(a => a.Uri);
    }

    /// <inheritdoc/>
    public bool CanResolve(string authority)
        => string.Equals(authority, Authority, StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc/>
    public Task<Asset?> ResolveAsync(Uri uri)
    {
        var result = this.assets.GetValueOrDefault(uri);
        return Task.FromResult(result);
    }

    private static IEnumerable<Asset> CreateBuiltInAssets()
    {
        // Basic shape geometries (1 LOD, 1 SubMesh "Main")
        yield return new GeometryAsset
        {
            Uri = new($"{BaseUri}BasicShapes/Cube"),
            Lods =
            [
                new MeshLod
                {
                    LodIndex = 0,
                    SubMeshes = [new SubMesh { Name = "Main", MaterialIndex = 0 }],
                },
            ],
        };

        yield return new GeometryAsset
        {
            Uri = new($"{BaseUri}BasicShapes/Sphere"),
            Lods =
            [
                new MeshLod
                {
                    LodIndex = 0,
                    SubMeshes = [new SubMesh { Name = "Main", MaterialIndex = 0 }],
                },
            ],
        };

        yield return new GeometryAsset
        {
            Uri = new($"{BaseUri}BasicShapes/Plane"),
            Lods =
            [
                new MeshLod
                {
                    LodIndex = 0,
                    SubMeshes = [new SubMesh { Name = "Main", MaterialIndex = 0 }],
                },
            ],
        };

        yield return new GeometryAsset
        {
            Uri = new($"{BaseUri}BasicShapes/Cylinder"),
            Lods =
            [
                new MeshLod
                {
                    LodIndex = 0,
                    SubMeshes = [new SubMesh { Name = "Main", MaterialIndex = 0 }],
                },
            ],
        };

        // Default material
        yield return new MaterialAsset
        {
            Uri = new($"{BaseUri}Materials/Default"),
        };
    }
}
