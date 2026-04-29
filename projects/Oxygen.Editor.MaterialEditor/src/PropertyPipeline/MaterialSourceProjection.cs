// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Oxygen.Assets.Import.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Projects an in-memory <see cref="MaterialSource"/> into the canonical
/// engine-schema shape used by <c>oxygen.material-descriptor.schema.json</c>.
/// </summary>
/// <remarks>
/// <para>
/// The editor's in-memory model uses a glTF-flavored layout
/// (<c>pbrMetallicRoughness</c>, <c>normalTexture</c>,
/// <c>occlusionTexture</c>) for ergonomic reasons. The cooker — and
/// hence the engine schema — uses the canonical Oxygen layout
/// (<c>parameters.base_color</c>, <c>parameters.metalness</c>,
/// <c>textures.base_color</c>, …). This projector is the single point
/// of translation; both <see cref="MaterialSchemaValidator"/> on save
/// and any future engine-shape on-disk writer go through here.
/// </para>
/// </remarks>
public static class MaterialSourceProjection
{
    public static JsonObject ToEngineJson(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);

        var parameters = new JsonObject
        {
            ["base_color"] = new JsonArray(
                source.PbrMetallicRoughness.BaseColorR,
                source.PbrMetallicRoughness.BaseColorG,
                source.PbrMetallicRoughness.BaseColorB,
                source.PbrMetallicRoughness.BaseColorA),
            ["metalness"] = source.PbrMetallicRoughness.MetallicFactor,
            ["roughness"] = source.PbrMetallicRoughness.RoughnessFactor,
            ["double_sided"] = source.DoubleSided,
        };

        if (source.NormalTexture is { } normal)
        {
            parameters["normal_scale"] = normal.Scale;
        }

        if (source.OcclusionTexture is { } occlusion)
        {
            parameters["ambient_occlusion"] = occlusion.Strength;
        }

        if (source.AlphaMode == MaterialAlphaMode.Mask)
        {
            parameters["alpha_cutoff"] = source.AlphaCutoff;
        }

        var textures = new JsonObject();
        if (source.PbrMetallicRoughness.BaseColorTexture is { } baseColorTex)
        {
            textures["base_color"] = new JsonObject { ["virtual_path"] = baseColorTex.Source };
        }

        if (source.NormalTexture is { } normalTex && !string.IsNullOrEmpty(normalTex.Source))
        {
            textures["normal"] = new JsonObject { ["virtual_path"] = normalTex.Source };
        }

        if (source.OcclusionTexture is { } occlusionTex && !string.IsNullOrEmpty(occlusionTex.Source))
        {
            textures["ambient_occlusion"] = new JsonObject { ["virtual_path"] = occlusionTex.Source };
        }

        var doc = new JsonObject
        {
            ["name"] = source.Name ?? string.Empty,
            ["alpha_mode"] = ToEngineAlphaMode(source.AlphaMode),
            ["parameters"] = parameters,
        };

        if (textures.Count > 0)
        {
            doc["textures"] = textures;
        }

        return doc;
    }

    private static string ToEngineAlphaMode(MaterialAlphaMode mode) => mode switch
    {
        MaterialAlphaMode.Opaque => "opaque",
        MaterialAlphaMode.Mask => "masked",
        MaterialAlphaMode.Blend => "blended",
        _ => "opaque",
    };
}
