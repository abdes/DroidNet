// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;
using Oxygen.Core;

namespace Oxygen.Assets;

/// <summary>
/// Provides a canonical list of built-in assets.
/// </summary>
/// <remarks>
/// This is the single source of truth for built-in assets. It is used by both the resolver
/// (load-by-URI) and the catalog (enumeration/search).
/// </remarks>
internal static class BuiltInAssets
{
    public static IEnumerable<Asset> Create()
    {
        // Basic shape geometries (1 LOD, 1 SubMesh "Main")
        yield return new GeometryAsset
        {
            Uri = new(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
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
            Uri = new(AssetUris.BuildGeneratedUri("BasicShapes/Sphere")),
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
            Uri = new(AssetUris.BuildGeneratedUri("BasicShapes/Plane")),
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
            Uri = new(AssetUris.BuildGeneratedUri("BasicShapes/Cylinder")),
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
            Uri = new(AssetUris.BuildGeneratedUri("Materials/Default")),
        };
    }
}
