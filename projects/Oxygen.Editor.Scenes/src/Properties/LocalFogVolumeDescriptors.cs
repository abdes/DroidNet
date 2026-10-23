// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for local fog volume inspector properties.
/// </summary>
public sealed class LocalFogVolumeDescriptors
{
    private LocalFogVolumeDescriptors()
    {
        this.EnabledDescriptor = CreateEnabledDescriptor();
        this.RadialFogExtinctionDescriptor = FloatDescriptor(
            SceneSchema.LocalFogVolume.RadialFogExtinction,
            "Density",
            static volume => volume.RadialFogExtinction,
            static (volume, value) => volume.RadialFogExtinction = value);
        this.HeightFogExtinctionDescriptor = FloatDescriptor(
            SceneSchema.LocalFogVolume.HeightFogExtinction,
            "Height Density",
            static volume => volume.HeightFogExtinction,
            static (volume, value) => volume.HeightFogExtinction = value);
        this.HeightFogFalloffDescriptor = FloatDescriptor(
            SceneSchema.LocalFogVolume.HeightFogFalloff,
            "Height Falloff",
            static volume => volume.HeightFogFalloff,
            static (volume, value) => volume.HeightFogFalloff = value);
        this.HeightFogOffsetDescriptor = FloatDescriptor(
            SceneSchema.LocalFogVolume.HeightFogOffset,
            "Height Offset",
            static volume => volume.HeightFogOffset,
            static (volume, value) => volume.HeightFogOffset = value);
        this.FogPhaseGDescriptor = FloatDescriptor(
            SceneSchema.LocalFogVolume.FogPhaseG,
            "Phase",
            static volume => volume.FogPhaseG,
            static (volume, value) => volume.FogPhaseG = value);
        this.FogAlbedoDescriptor = VectorDescriptor(
            SceneSchema.LocalFogVolume.FogAlbedo,
            "Albedo",
            static volume => volume.FogAlbedo,
            static (volume, value) => volume.FogAlbedo = value);
        this.FogEmissiveDescriptor = VectorDescriptor(
            SceneSchema.LocalFogVolume.FogEmissive,
            "Emissive",
            static volume => volume.FogEmissive,
            static (volume, value) => volume.FogEmissive = value);
        this.SortPriorityDescriptor = CreateSortPriorityDescriptor();
        this.ById = this.IndexById();
    }

    /// <summary>Gets the descriptor for volume enablement.</summary>
    public PropertyDescriptor<bool> EnabledDescriptor { get; }

    /// <summary>Gets the descriptor for the radial extinction.</summary>
    public PropertyDescriptor<float> RadialFogExtinctionDescriptor { get; }

    /// <summary>Gets the descriptor for the height-based extinction.</summary>
    public PropertyDescriptor<float> HeightFogExtinctionDescriptor { get; }

    /// <summary>Gets the descriptor for the height falloff.</summary>
    public PropertyDescriptor<float> HeightFogFalloffDescriptor { get; }

    /// <summary>Gets the descriptor for the height offset.</summary>
    public PropertyDescriptor<float> HeightFogOffsetDescriptor { get; }

    /// <summary>Gets the descriptor for the phase anisotropy.</summary>
    public PropertyDescriptor<float> FogPhaseGDescriptor { get; }

    /// <summary>Gets the descriptor for the fog albedo.</summary>
    public PropertyDescriptor<Vector3> FogAlbedoDescriptor { get; }

    /// <summary>Gets the descriptor for the emitted luminance.</summary>
    public PropertyDescriptor<Vector3> FogEmissiveDescriptor { get; }

    /// <summary>Gets the descriptor for the composition order.</summary>
    public PropertyDescriptor<int> SortPriorityDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the canonical local fog volume descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static LocalFogVolumeDescriptors Build() => new();

    private static PropertyDescriptor<bool> CreateEnabledDescriptor()
        => new(
            id: SceneSchema.LocalFogVolume.Enabled,
            reader: static target => ((LocalFogVolumeComponent)target).Enabled,
            writer: static (target, value) => ((LocalFogVolumeComponent)target).Enabled = value,
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation("enabled", new EditorAnnotation { Group = "Local Fog Volume", Label = "Enabled", Renderer = "toggle" }));

    private static PropertyDescriptor<int> CreateSortPriorityDescriptor()
        => new(
            id: SceneSchema.LocalFogVolume.SortPriority,
            reader: static target => ((LocalFogVolumeComponent)target).SortPriority,
            writer: static (target, value) => ((LocalFogVolumeComponent)target).SortPriority = value,
            validator: PropertyValidators.Of(SceneSchema.LocalFogVolume.SortPriority, "Sort priority"),
            annotation: Annotation("sort_priority", new EditorAnnotation { Group = "Local Fog Volume", Label = "Sort Priority", Renderer = "numberbox", Step = 1 }));

    private static PropertyDescriptor<float> FloatDescriptor(
        PropertyId<float> id,
        string label,
        Func<LocalFogVolumeComponent, float> read,
        Action<LocalFogVolumeComponent, float> write)
        => new(
            id: id,
            reader: target => read((LocalFogVolumeComponent)target),
            writer: (target, value) => write((LocalFogVolumeComponent)target, value),
            validator: PropertyValidators.Of(id, label),
            annotation: Annotation(id.Id.JsonPointer.TrimStart('/'), new EditorAnnotation { Group = "Local Fog Volume", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<Vector3> VectorDescriptor(
        PropertyId<Vector3> id,
        string label,
        Func<LocalFogVolumeComponent, Vector3> read,
        Action<LocalFogVolumeComponent, Vector3> write)
        => new(
            id: id,
            reader: target => read((LocalFogVolumeComponent)target),
            writer: (target, value) => write((LocalFogVolumeComponent)target, value),
            validator: PropertyValidators.Of(id, label),
            annotation: Annotation(id.Id.JsonPointer.TrimStart('/'), new EditorAnnotation { Group = "Local Fog Volume", Label = label, Renderer = "color-rgb" }));

    private static EditorAnnotation Annotation(string property, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get("#/definitions/local_fog_volume/" + property, fallback);

    private Dictionary<PropertyId, PropertyDescriptor> IndexById()
        => new()
        {
            [this.EnabledDescriptor.Id] = this.EnabledDescriptor,
            [this.RadialFogExtinctionDescriptor.Id] = this.RadialFogExtinctionDescriptor,
            [this.HeightFogExtinctionDescriptor.Id] = this.HeightFogExtinctionDescriptor,
            [this.HeightFogFalloffDescriptor.Id] = this.HeightFogFalloffDescriptor,
            [this.HeightFogOffsetDescriptor.Id] = this.HeightFogOffsetDescriptor,
            [this.FogPhaseGDescriptor.Id] = this.FogPhaseGDescriptor,
            [this.FogAlbedoDescriptor.Id] = this.FogAlbedoDescriptor,
            [this.FogEmissiveDescriptor.Id] = this.FogEmissiveDescriptor,
            [this.SortPriorityDescriptor.Id] = this.SortPriorityDescriptor,
        };
}
