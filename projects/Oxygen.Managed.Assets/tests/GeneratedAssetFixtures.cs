// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>Metadata inputs for generic generated-catalog and resolver tests.</summary>
internal static class GeneratedAssetFixtures
{
    public static IEnumerable<Asset> Create()
    {
        foreach (var name in new[] { "Cube", "Sphere", "Plane", "Cylinder" })
        {
            yield return new GeometryAsset
            {
                Uri = AssetUris.BuildGeneratedUri("BasicShapes/" + name),
                Lods = [new MeshLod { LodIndex = 0, SubMeshes = [new SubMesh { Name = "Main", MaterialIndex = 0 }] }],
            };
        }

        yield return new MaterialAsset { Uri = AssetUris.BuildGeneratedUri("Materials/Default") };
    }
}
