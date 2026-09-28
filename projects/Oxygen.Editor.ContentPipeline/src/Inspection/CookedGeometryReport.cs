// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;

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

        var geometries = ImmutableDictionary.CreateBuilder<Guid, Inventory>();
        foreach (var row in document.RootElement.GetProperty("geometries").EnumerateArray())
        {
            var key = row.GetProperty("geometry_asset_key").GetGuid();
            var ids = new HashSet<Guid>();
            var locations = new HashSet<(uint lod, uint submesh)>();
            var slots = ImmutableArray.CreateBuilder<GeometryMaterialSlot>();
            foreach (var slot in row.GetProperty("slots").EnumerateArray())
            {
                var slotId = slot.GetProperty("slot_id").GetGuid();
                var bindings = slot.GetProperty("bindings").EnumerateArray().Select(static binding => new GeometryMaterialSlotBinding(
                    binding.GetProperty("lod_index").GetUInt32(), binding.GetProperty("submesh_index").GetUInt32(), binding.GetProperty("default_material_key").GetGuid())).ToImmutableArray();
                if (!ids.Add(slotId) || bindings.Any(binding => !locations.Add((binding.LodIndex, binding.SubmeshIndex))))
                {
                    throw new InvalidDataException("The native geometry inventory contains duplicate slot identities or surface bindings.");
                }

                slots.Add(new(slotId, slot.GetProperty("display_name").GetString()!, bindings));
            }

            if (!geometries.TryAdd(key, new(row.GetProperty("layout_revision").GetString()!, slots.ToImmutable())))
            {
                throw new InvalidDataException("The native geometry report contains duplicate asset keys.");
            }
        }

        return new(geometries.ToImmutable());
    }

    /// <summary>Finds the exact native key while preserving the requested authored URI.</summary>
    /// <param name="geometryUri">The authored identity.</param>
    /// <param name="nativeGeometryKey">The native key to resolve.</param>
    /// <returns>The matching inventory, or null.</returns>
    public GeometryMaterialSlotMetadata? Find(Uri geometryUri, Guid nativeGeometryKey)
        => this.geometries.TryGetValue(nativeGeometryKey, out var inventory) ? new(geometryUri, nativeGeometryKey, inventory.LayoutRevision, inventory.Slots) : null;

    /// <summary>Reads a native command result scoped to one virtual path.</summary>
    /// <param name="geometryUri">The authored identity.</param>
    /// <returns>The sole inventory, or null when the root has no matching geometry.</returns>
    public GeometryMaterialSlotMetadata? SingleOrDefault(Uri geometryUri)
        => this.geometries.Count switch
        {
            0 => null,
            1 => this.Find(geometryUri, this.geometries.Keys.Single()),
            _ => throw new InvalidDataException("The native geometry query returned more than one asset."),
        };

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(CookedGeometryReport).Assembly.GetManifestResourceStream("oxygen.cooked-geometries.schema.json")
            ?? throw new InvalidOperationException("The native geometry metadata schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }

    private sealed record Inventory(string LayoutRevision, ImmutableArray<GeometryMaterialSlot> Slots);
}
