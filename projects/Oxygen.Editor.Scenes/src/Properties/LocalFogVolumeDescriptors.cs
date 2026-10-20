// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

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
            "/radial_fog_extinction",
            "Density",
            static volume => volume.RadialFogExtinction,
            static (volume, value) => volume.RadialFogExtinction = value,
            minimum: 0f,
            maximum: null);
        this.HeightFogExtinctionDescriptor = FloatDescriptor(
            "/height_fog_extinction",
            "Height Density",
            static volume => volume.HeightFogExtinction,
            static (volume, value) => volume.HeightFogExtinction = value,
            minimum: 0f,
            maximum: null);
        this.HeightFogFalloffDescriptor = FloatDescriptor(
            "/height_fog_falloff",
            "Height Falloff",
            static volume => volume.HeightFogFalloff,
            static (volume, value) => volume.HeightFogFalloff = value,
            minimum: 0f,
            maximum: null);
        this.HeightFogOffsetDescriptor = FloatDescriptor(
            "/height_fog_offset",
            "Height Offset",
            static volume => volume.HeightFogOffset,
            static (volume, value) => volume.HeightFogOffset = value,
            minimum: null,
            maximum: null);
        this.FogPhaseGDescriptor = FloatDescriptor(
            "/fog_phase_g",
            "Phase",
            static volume => volume.FogPhaseG,
            static (volume, value) => volume.FogPhaseG = value,
            minimum: 0f,
            maximum: 0.999f);
        this.FogAlbedoDescriptor = VectorDescriptor(
            "/fog_albedo",
            "Albedo",
            static volume => volume.FogAlbedo,
            static (volume, value) => volume.FogAlbedo = value,
            maximum: 1f);
        this.FogEmissiveDescriptor = VectorDescriptor(
            "/fog_emissive",
            "Emissive",
            static volume => volume.FogEmissive,
            static (volume, value) => volume.FogEmissive = value,
            maximum: null);
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
            id: new PropertyId<bool>(SceneProperties.LocalFogVolumeKind, "/enabled"),
            reader: static target => ((LocalFogVolumeComponent)target).Enabled,
            writer: static (target, value) => ((LocalFogVolumeComponent)target).Enabled = value,
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation("enabled", new EditorAnnotation { Group = "Local Fog Volume", Label = "Enabled", Renderer = "toggle" }));

    private static PropertyDescriptor<int> CreateSortPriorityDescriptor()
        => new(
            id: new PropertyId<int>(SceneProperties.LocalFogVolumeKind, "/sort_priority"),
            reader: static target => ((LocalFogVolumeComponent)target).SortPriority,
            writer: static (target, value) => ((LocalFogVolumeComponent)target).SortPriority = value,
            validator: static value => value is >= -127 and <= 127
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.LocalFogVolumeInvalid, "Sort priority must be between -127 and 127."),
            annotation: Annotation("sort_priority", new EditorAnnotation { Group = "Local Fog Volume", Label = "Sort Priority", Renderer = "numberbox", Step = 1 }));

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<LocalFogVolumeComponent, float> read,
        Action<LocalFogVolumeComponent, float> write,
        float? minimum,
        float? maximum)
        => new(
            id: new PropertyId<float>(SceneProperties.LocalFogVolumeKind, pointer),
            reader: target => read((LocalFogVolumeComponent)target),
            writer: (target, value) => write((LocalFogVolumeComponent)target, value),
            validator: value => Validate(label, minimum, maximum, value),
            annotation: Annotation(pointer.TrimStart('/'), new EditorAnnotation { Group = "Local Fog Volume", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<Vector3> VectorDescriptor(
        string pointer,
        string label,
        Func<LocalFogVolumeComponent, Vector3> read,
        Action<LocalFogVolumeComponent, Vector3> write,
        float? maximum)
        => new(
            id: new PropertyId<Vector3>(SceneProperties.LocalFogVolumeKind, pointer),
            reader: target => read((LocalFogVolumeComponent)target),
            writer: (target, value) => write((LocalFogVolumeComponent)target, value),
            validator: value =>
            {
                var x = Validate(label, 0f, maximum, value.X);
                if (!x.IsValid)
                {
                    return x;
                }

                var y = Validate(label, 0f, maximum, value.Y);
                return y.IsValid ? Validate(label, 0f, maximum, value.Z) : y;
            },
            annotation: Annotation(pointer.TrimStart('/'), new EditorAnnotation { Group = "Local Fog Volume", Label = label, Renderer = "color-rgb" }));

    private static ValidationResult Validate(string label, float? minimum, float? maximum, float value)
        => !float.IsFinite(value)
            ? ValidationResult.Fail(SceneDiagnosticCodes.LocalFogVolumeInvalid, $"{label} must be finite.")
            : (minimum is { } low && value < low) || (maximum is { } high && value > high)
            ? ValidationResult.Fail(SceneDiagnosticCodes.LocalFogVolumeInvalid, RangeMessage(label, minimum, maximum))
            : ValidationResult.Ok;

    private static string RangeMessage(string label, float? minimum, float? maximum)
        => maximum is { } high
            ? string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{label} must be between {minimum ?? float.NegativeInfinity} and {high}.")
            : string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{label} must be at least {minimum}.");

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
