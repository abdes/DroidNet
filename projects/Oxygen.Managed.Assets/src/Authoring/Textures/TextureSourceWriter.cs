// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>Writes the engine's texture descriptor, <c>*.otex.json</c>, keeping the fields the model does not name.</summary>
public static class TextureSourceWriter
{
    /// <summary>Creates an independently owned, schema-validated descriptor.</summary>
    /// <param name="texture">The texture's source.</param>
    /// <returns>The descriptor, including the fields kept from the one it was read from.</returns>
    /// <exception cref="AssetFormatException">The source does not make a valid descriptor.</exception>
    public static JsonObject ToJson(TextureSource texture)
    {
        ArgumentNullException.ThrowIfNull(texture);
        var descriptor = texture.Descriptor is { } saved ? JsonNode.Parse(saved.GetRawText())!.AsObject() : new JsonObject();
        Set(descriptor, "name", texture.Name);
        Set(descriptor, "virtual_path", texture.VirtualPath?.VirtualPath);
        descriptor["source"] = texture.Source;
        Set(descriptor, "sources", texture.Sources.Count == 0 ? null : new JsonArray([.. texture.Sources.Select(ToJson)]));
        Set(descriptor, "intent", texture.Intent);
        SetNested(descriptor, "decode", "color_space", texture.ColorSpace);
        SetNested(descriptor, "output", "format", texture.Format);
        if (texture.Cube is { } cube)
        {
            SetNested(descriptor, "cube", "cubemap", cube.Cubemap);
            SetNested(descriptor, "cube", "equirect_to_cube", cube.EquirectToCube);
            SetNested(descriptor, "cube", "cube_face_size", cube.FaceSize);
            SetNested(descriptor, "cube", "cube_layout", cube.Layout);
        }
        else
        {
            _ = descriptor.Remove("cube");
        }

        _ = TextureSourceReader.Validate(JsonSerializer.SerializeToElement(descriptor));
        return descriptor;
    }

    private static JsonObject ToJson(TextureSourceMapping mapping)
    {
        var json = new JsonObject { ["file"] = mapping.File };
        Set(json, "layer", mapping.Layer);
        Set(json, "mip", mapping.Mip);
        Set(json, "slice", mapping.Slice);
        return json;
    }

    private static void Set(JsonObject parent, string name, JsonNode? value)
    {
        if (value is null)
        {
            _ = parent.Remove(name);
        }
        else
        {
            parent[name] = value;
        }
    }

    // A field the source no longer has leaves its group, and an emptied group leaves the descriptor.
    private static void SetNested(JsonObject descriptor, string group, string name, JsonNode? value)
    {
        if (descriptor[group] is JsonObject existing)
        {
            Set(existing, name, value);
            if (existing.Count == 0)
            {
                _ = descriptor.Remove(group);
            }
        }
        else if (value is not null)
        {
            descriptor[group] = new JsonObject { [name] = value };
        }
    }
}
