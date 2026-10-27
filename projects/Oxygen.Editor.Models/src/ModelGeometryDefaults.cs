// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Models;

/// <summary>
/// Reads and writes the asset defaults a model's import writes into its geometries: each geometry's LOD policy and its
/// surfaces' visibility and shadow casting, which every placement of the model starts from (content pipeline 20.5).
/// </summary>
/// <remarks>
/// The settings key them by geometry anchor, the geometry's storage name in the source, which the record's surface
/// provenance maps from the cooked geometry's key. A default that is removed takes the import's rule again; an anchor or
/// surface left without defaults is removed, so the settings hold only what the user set.
/// </remarks>
public static class ModelGeometryDefaults
{
    private const string LodPolicyKey = "lod_policy";
    private const string SurfacesKey = "surfaces";

    /// <summary>Gets the anchor of the cooked geometry with <paramref name="geometryKey"/>, as the last import recorded it.</summary>
    /// <param name="surfaceProvenance">The record's surface provenance.</param>
    /// <param name="geometryKey">The cooked geometry's asset key.</param>
    /// <returns>The geometry's anchor, or <see langword="null"/> when the last import produced no such geometry.</returns>
    public static string? AnchorOf(JsonElement surfaceProvenance, Guid geometryKey)
    {
        if (surfaceProvenance.ValueKind != JsonValueKind.Object
            || !surfaceProvenance.TryGetProperty("geometries", out var geometries)
            || geometries.ValueKind != JsonValueKind.Array)
        {
            return null;
        }

        return geometries.EnumerateArray()
            .Where(geometry => geometry.TryGetProperty("geometry_asset_key", out var key) && key.TryGetGuid(out var parsed) && parsed == geometryKey)
            .Select(static geometry => geometry.GetProperty("source_geometry_anchor").GetString())
            .FirstOrDefault();
    }

    /// <summary>Gets the LOD policy the settings set for a geometry.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="anchor">The geometry's anchor.</param>
    /// <returns>The policy, or <see langword="null"/> when the import's rule applies.</returns>
    public static LodPolicy? LodPolicyOf(JsonElement settings, string anchor)
        => DefaultsOf(settings, anchor) is { } defaults && defaults.TryGetProperty(LodPolicyKey, out var policy)
            ? policy.Deserialize(SceneJsonContext.Default.LodPolicy)
            : null;

    /// <summary>Gets the settings with a geometry's LOD policy set, or removed for <see langword="null"/>.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="anchor">The geometry's anchor.</param>
    /// <param name="policy">The policy, or <see langword="null"/> to take the import's rule.</param>
    /// <returns>The changed settings.</returns>
    public static JsonElement WithLodPolicy(JsonElement settings, string anchor, LodPolicy? policy)
        => Change(settings, anchor, defaults =>
        {
            if (policy is null)
            {
                _ = defaults.Remove(LodPolicyKey);
            }
            else
            {
                defaults[LodPolicyKey] = JsonSerializer.SerializeToNode(policy, SceneJsonContext.Default.LodPolicy);
            }
        });

    /// <summary>Gets the value the settings set for a surface's flag.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="anchor">The geometry's anchor.</param>
    /// <param name="surface">The surface's ID.</param>
    /// <param name="flag">The flag.</param>
    /// <returns>The value, or <see langword="null"/> when the scene schema's default applies.</returns>
    public static bool? SurfaceFlagOf(JsonElement settings, string anchor, Guid surface, ModelSurfaceFlag flag)
        => DefaultsOf(settings, anchor) is { } defaults
            && defaults.TryGetProperty(SurfacesKey, out var surfaces)
            && surfaces.TryGetProperty(surface.ToString("D"), out var values)
            && values.TryGetProperty(KeyOf(flag), out var value)
            && value.ValueKind is JsonValueKind.True or JsonValueKind.False
                ? value.GetBoolean()
                : null;

    /// <summary>Gets the settings with a surface's flag set, or removed for <see langword="null"/>.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="anchor">The geometry's anchor.</param>
    /// <param name="surface">The surface's ID.</param>
    /// <param name="flag">The flag.</param>
    /// <param name="value">The value, or <see langword="null"/> to take the scene schema's default.</param>
    /// <returns>The changed settings.</returns>
    public static JsonElement WithSurfaceFlag(JsonElement settings, string anchor, Guid surface, ModelSurfaceFlag flag, bool? value)
        => Change(settings, anchor, defaults =>
        {
            var surfaces = defaults[SurfacesKey]?.DeepClone() as JsonObject ?? [];
            var id = surface.ToString("D");
            var values = surfaces[id]?.DeepClone() as JsonObject ?? [];
            if (value is { } set)
            {
                values[KeyOf(flag)] = set;
            }
            else
            {
                _ = values.Remove(KeyOf(flag));
            }

            if (values.Count == 0)
            {
                _ = surfaces.Remove(id);
            }
            else
            {
                surfaces[id] = values;
            }

            if (surfaces.Count == 0)
            {
                _ = defaults.Remove(SurfacesKey);
            }
            else
            {
                defaults[SurfacesKey] = surfaces;
            }
        });

    private static string KeyOf(ModelSurfaceFlag flag) => flag switch
    {
        ModelSurfaceFlag.Visible => "visible",
        ModelSurfaceFlag.CastsShadows => "casts_shadows",
        _ => throw new ArgumentOutOfRangeException(nameof(flag), flag, "Unknown surface flag."),
    };

    private static JsonElement? DefaultsOf(JsonElement settings, string anchor)
        => settings.ValueKind == JsonValueKind.Object
            && settings.TryGetProperty(ModelImportSettingCatalog.GeometryDefaultsKey, out var geometries)
            && geometries.ValueKind == JsonValueKind.Object
            && geometries.TryGetProperty(anchor, out var defaults)
            && defaults.ValueKind == JsonValueKind.Object
                ? defaults
                : null;

    // Applies a change to one geometry's defaults; an empty geometry, and then an empty map, leaves the settings.
    private static JsonElement Change(JsonElement settings, string anchor, Action<JsonObject> change)
    {
        ArgumentException.ThrowIfNullOrEmpty(anchor);
        var copy = settings.ValueKind == JsonValueKind.Object ? JsonNode.Parse(settings.GetRawText())!.AsObject() : [];
        var geometries = copy[ModelImportSettingCatalog.GeometryDefaultsKey]?.DeepClone() as JsonObject ?? [];
        var defaults = geometries[anchor]?.DeepClone() as JsonObject ?? [];
        change(defaults);
        if (defaults.Count == 0)
        {
            _ = geometries.Remove(anchor);
        }
        else
        {
            geometries[anchor] = defaults;
        }

        if (geometries.Count == 0)
        {
            _ = copy.Remove(ModelImportSettingCatalog.GeometryDefaultsKey);
        }
        else
        {
            copy[ModelImportSettingCatalog.GeometryDefaultsKey] = geometries;
        }

        return JsonSerializer.SerializeToElement(copy);
    }
}
