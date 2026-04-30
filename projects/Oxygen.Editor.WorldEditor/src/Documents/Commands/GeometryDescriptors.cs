// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for geometry component inspector properties.
/// </summary>
internal sealed class GeometryDescriptors
{
    private GeometryDescriptors(
        PropertyDescriptor<Uri?> geometryUri,
        PropertyDescriptor<Uri?> materialSlot0Uri)
    {
        this.GeometryUri = new PropertyId<Uri?>(geometryUri.Id);
        this.MaterialSlot0Uri = new PropertyId<Uri?>(materialSlot0Uri.Id);
        this.GeometryUriDescriptor = geometryUri;
        this.MaterialSlot0UriDescriptor = materialSlot0Uri;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [geometryUri.Id] = geometryUri,
            [materialSlot0Uri.Id] = materialSlot0Uri,
        };
    }

    /// <summary>Gets the typed property id for the geometry asset URI.</summary>
    internal PropertyId<Uri?> GeometryUri { get; }

    /// <summary>Gets the typed property id for the first material override slot URI.</summary>
    internal PropertyId<Uri?> MaterialSlot0Uri { get; }

    /// <summary>Gets the descriptor for the geometry asset URI.</summary>
    internal PropertyDescriptor<Uri?> GeometryUriDescriptor { get; }

    /// <summary>Gets the descriptor for the first material override slot URI.</summary>
    internal PropertyDescriptor<Uri?> MaterialSlot0UriDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical geometry descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    internal static GeometryDescriptors Build()
        => new(
            geometryUri: new PropertyDescriptor<Uri?>(
                id: new PropertyId<Uri?>(SceneDocumentCommandService.GeometryKind, "/geometry_uri"),
                reader: static target => ((GeometryComponent)target).Geometry?.Uri,
                writer: static (target, value) => ((GeometryComponent)target).Geometry = value is null ? null : new AssetReference<GeometryAsset>(value),
                validator: static value => value is null
                    ? ValidationResult.Fail(SceneDiagnosticCodes.GeometryReferenceRequired, "A geometry component must reference a geometry asset.")
                    : ValidationResult.Ok,
                annotation: SceneEditorSchemaAnnotations.Get(
                    "#/definitions/renderable/geometry_ref",
                    new EditorAnnotation { Group = "Geometry", Label = "Geometry", Renderer = "asset-picker" }),
                engineCommandKey: "geometry.geometry_uri"),
            materialSlot0Uri: new PropertyDescriptor<Uri?>(
                id: new PropertyId<Uri?>(SceneDocumentCommandService.GeometryKind, "/material_slots/0/material_uri"),
                reader: static target => NormalizeMaterialUri(((GeometryComponent)target).OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri),
                writer: static (target, value) => ApplyMaterialSlot((GeometryComponent)target, value),
                validator: static _ => ValidationResult.Ok,
                annotation: SceneEditorSchemaAnnotations.Get(
                    "#/definitions/renderable/material_ref",
                    new EditorAnnotation { Group = "Geometry", Label = "Material", Renderer = "asset-picker" }),
                engineCommandKey: "geometry.material_slots.0.material_uri"));

    private static Uri? NormalizeMaterialUri(Uri? uri)
        => uri is not null && string.Equals(uri.ToString(), "asset:///__uninitialized__", StringComparison.OrdinalIgnoreCase)
            ? null
            : uri;

    private static void ApplyMaterialSlot(GeometryComponent geometry, Uri? materialUri)
    {
        var slot = geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault();
        if (slot is null)
        {
            slot = new MaterialsSlot();
            geometry.OverrideSlots.Add(slot);
        }

        slot.Material = new AssetReference<MaterialAsset>(materialUri ?? new Uri("asset:///__uninitialized__"));
    }
}
