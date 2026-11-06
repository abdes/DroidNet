// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>A geometry descriptor (<c>*.ogeo.json</c>).</summary>
public sealed class GeometryDocument : AuthoredDocument<GeometryDocument>
{
    /// <summary>The engine's default material, which the minimal descriptor's submesh uses.</summary>
    public const string DefaultMaterial = "/Engine/Materials/Default.omat";

    /// <summary>Initializes a new instance of the <see cref="GeometryDocument"/> class.</summary>
    /// <param name="identity">The geometry's identity.</param>
    public GeometryDocument(AssetPath identity)
        : base(identity, new JsonObject { ["name"] = StemOf(identity) })
    {
        _ = this.Append(
            "/lods",
            new JsonObject
            {
                ["name"] = "LOD0",
                ["mesh_type"] = "procedural",
                ["procedural"] = new JsonObject { ["generator"] = "Capsule", ["mesh_name"] = "Capsule" },
                ["submeshes"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["surface_id"] = "018f8f8f-1111-7111-8111-111111111111",
                        ["views"] = new JsonArray { new JsonObject { ["view_ref"] = "__all__" } },
                    },
                },
            });
        _ = this.SubmeshMaterial(0, 0, DefaultMaterial);
    }

    /// <summary>Makes a submesh use the material <paramref name="target"/>.</summary>
    /// <param name="lod">The LOD's index.</param>
    /// <param name="submesh">The submesh's index in the LOD.</param>
    /// <param name="target">The material's identity.</param>
    /// <returns>This document.</returns>
    public GeometryDocument SubmeshMaterial(int lod, int submesh, string target)
        => this.Reference(string.Create(CultureInfo.InvariantCulture, $"/lods/{lod}/submeshes/{submesh}/material_ref"), target);
}
