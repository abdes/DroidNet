// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for geometry component inspector properties.
/// </summary>
public sealed class GeometryDescriptors
{
    private GeometryDescriptors(PropertyDescriptor<Uri?> geometryUri)
    {
        this.GeometryUri = new PropertyId<Uri?>(geometryUri.Id);
        this.GeometryUriDescriptor = geometryUri;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor> { [geometryUri.Id] = geometryUri };
    }

    /// <summary>Gets the typed property id for the geometry asset URI.</summary>
    public PropertyId<Uri?> GeometryUri { get; }

    /// <summary>Gets the descriptor for the geometry asset URI.</summary>
    public PropertyDescriptor<Uri?> GeometryUriDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical geometry descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    public static GeometryDescriptors Build()
        => new(
            geometryUri: new PropertyDescriptor<Uri?>(
                id: new PropertyId<Uri?>(SceneProperties.GeometryKind, "/geometry_uri"),
                reader: static target => ((GeometryComponent)target).Geometry,
                writer: static (target, value) => ((GeometryComponent)target).Geometry = value ?? throw new ArgumentNullException(nameof(value)),
                validator: static value => value is null
                    ? ValidationResult.Fail(SceneDiagnosticCodes.GeometryReferenceRequired, "A geometry component must reference a geometry asset.")
                    : ValidationResult.Ok,
                annotation: SceneEditorSchemaAnnotations.Get(
                    "#/definitions/renderable/geometry_ref",
                    new EditorAnnotation { Group = "Geometry", Label = "Geometry", Renderer = "asset-picker" })));
}
