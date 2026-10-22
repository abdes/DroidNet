// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for geometry component inspector properties.
/// </summary>
public sealed class GeometryDescriptors
{
    private GeometryDescriptors(PropertyDescriptor<AssetPath?> geometryUri)
    {
        this.GeometryUriDescriptor = geometryUri;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor> { [geometryUri.Id] = geometryUri };
    }

    /// <summary>Gets the descriptor for the geometry asset URI.</summary>
    public PropertyDescriptor<AssetPath?> GeometryUriDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical geometry descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    public static GeometryDescriptors Build()
        => new(
            geometryUri: new PropertyDescriptor<AssetPath?>(
                id: SceneSchema.Geometry.GeometryUri,
                reader: static target => ((GeometryComponent)target).Geometry,
                writer: static (target, value) => ((GeometryComponent)target).Geometry = value ?? throw new ArgumentNullException(nameof(value)),
                validator: static value => value is null
                    ? ValidationResult.Fail(SceneDiagnosticCodes.GeometryReferenceRequired, "A geometry component must reference a geometry asset.")
                    : ValidationResult.Ok,
                annotation: SceneEditorSchemaAnnotations.Get(
                    "#/definitions/renderable/geometry_ref",
                    new EditorAnnotation { Group = "Geometry", Label = "Geometry", Renderer = "asset-picker" })));
}
