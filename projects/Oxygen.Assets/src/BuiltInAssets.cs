// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;

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
    private const string BaseUri = "asset:///Generated/";

    public static IEnumerable<Asset> Create()
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
