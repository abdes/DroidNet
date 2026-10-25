// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Schema-validated native inventories, without managed identity or hash generation.</summary>
public sealed class CookedGeometryReport
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);
    private readonly ImmutableDictionary<Guid, Inventory> geometries;

    private CookedGeometryReport(ImmutableDictionary<Guid, Inventory> geometries) => this.geometries = geometries;

    /// <summary>Validates the shared native schema and rejects ambiguous identities or bindings.</summary>
    /// <param name="json">The native report.</param>
    /// <returns>Detached typed metadata.</returns>
    public static CookedGeometryReport Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!Schema.Value.Evaluate(document.RootElement).IsValid)
        {
            throw new InvalidDataException("The native geometry report does not match its schema.");
        }

        var inventories = ImmutableDictionary.CreateBuilder<Guid, Inventory>();
        foreach (var row in document.RootElement.GetProperty("geometries").EnumerateArray())
        {
            var key = row.GetProperty("geometry_asset_key").GetGuid();
            var ids = new HashSet<Guid>();
            var locations = new HashSet<(uint Lod, uint Submesh)>();
            var surfaces = ImmutableArray.CreateBuilder<GeometrySurface>();
            foreach (var surface in row.GetProperty("surfaces").EnumerateArray())
            {
                var surfaceId = surface.GetProperty("surface_id").GetGuid();
                var bindings = surface.GetProperty("bindings").EnumerateArray().Select(static binding => new GeometrySurfaceBinding(
                    binding.GetProperty("lod_index").GetUInt32(), binding.GetProperty("submesh_index").GetUInt32(), binding.GetProperty("default_material_key").GetGuid())).ToImmutableArray();
                if (!ids.Add(surfaceId) || bindings.Any(binding => !locations.Add((binding.LodIndex, binding.SubmeshIndex))))
                {
                    throw new InvalidDataException("The native geometry inventory contains duplicate surface identities or surface bindings.");
                }

                if (bindings.Any(static binding => binding.LodIndex >= LodPolicy.MaxLodCount))
                {
                    throw new InvalidDataException($"The native geometry inventory binds a LOD beyond the {LodPolicy.MaxLodCount} a geometry has.");
                }

                surfaces.Add(new(
                    surfaceId,
                    surface.GetProperty("display_name").GetString()!,
                    surface.GetProperty("visible").GetBoolean(),
                    surface.GetProperty("casts_shadows").GetBoolean(),
                    bindings));
            }

            if (!inventories.TryAdd(key, new(row.GetProperty("layout_revision").GetString()!, surfaces.ToImmutable(), ReadLodPolicy(row.GetProperty("lod_policy")))))
            {
                throw new InvalidDataException("The native geometry report contains duplicate asset keys.");
            }
        }

        return new(inventories.ToImmutable());
    }

    /// <summary>Finds the exact native key while preserving the requested authored URI.</summary>
    /// <param name="geometryUri">The authored identity.</param>
    /// <param name="nativeGeometryKey">The native key to resolve.</param>
    /// <returns>The matching inventory, or null.</returns>
    public GeometrySurfaceMetadata? Find(AssetPath geometryUri, Guid nativeGeometryKey)
        => this.geometries.TryGetValue(nativeGeometryKey, out var inventory) ? new(geometryUri, nativeGeometryKey, inventory.LayoutRevision, inventory.Surfaces, inventory.LodPolicy) : null;

    /// <summary>Reads a native command result scoped to one virtual path.</summary>
    /// <param name="geometryUri">The authored identity.</param>
    /// <returns>The sole inventory, or null when the root has no matching geometry.</returns>
    public GeometrySurfaceMetadata? SingleOrDefault(AssetPath geometryUri)
        => this.geometries.Count switch
        {
            0 => null,
            1 => this.Find(geometryUri, this.geometries.Keys.Single()),
            _ => throw new InvalidDataException("The native geometry query returned more than one asset."),
        };

    // The policy's JSON is the scene's (scene model 5.7), so the scene's reader reads it and applies the engine's rules.
    private static LodPolicy ReadLodPolicy(JsonElement json)
    {
        try
        {
            return (LodPolicy)SourceOverridesJson.ReadValue(SceneSchema.Geometry.LodPolicy.Id, json);
        }
        catch (JsonException error)
        {
            throw new InvalidDataException("The native geometry report has an invalid LOD policy. " + error.Message, error);
        }
    }

    private static JsonSchema LoadSchema() => CookedReportSchemas.Load("oxygen.cooked-geometries.schema.json");

    private sealed record Inventory(string LayoutRevision, ImmutableArray<GeometrySurface> Surfaces, LodPolicy LodPolicy);
}
