// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for directional light inspector properties.
/// </summary>
public sealed class DirectionalLightDescriptors
{
    private DirectionalLightDescriptors(
        PropertyDescriptor<Vector3> color,
        PropertyDescriptor<float> intensityLux,
        PropertyDescriptor<AtmosphereLightSlot> atmosphereSlot,
        PropertyDescriptor<bool> usePerPixelAtmosphereTransmittance,
        PropertyDescriptor<bool> castsShadows,
        PropertyDescriptor<bool> affectsWorld,
        PropertyDescriptor<float> angularSizeRadians,
        PropertyDescriptor<float> exposureCompensation,
        PropertyDescriptor<Vector3> atmosphereDiskLuminanceScaleRgb,
        PropertyDescriptor<float> shadowBias,
        PropertyDescriptor<float> shadowNormalBias,
        PropertyDescriptor<bool> contactShadows,
        PropertyDescriptor<ShadowResolutionHint> shadowResolutionHint,
        PropertyDescriptor<int> cascadeCount,
        PropertyDescriptor<DirectionalCsmSplitMode> splitMode,
        PropertyDescriptor<float> maxShadowDistance,
        PropertyDescriptor<float> cascadeDistance0,
        PropertyDescriptor<float> cascadeDistance1,
        PropertyDescriptor<float> cascadeDistance2,
        PropertyDescriptor<float> cascadeDistance3,
        PropertyDescriptor<float> distributionExponent,
        PropertyDescriptor<float> transitionFraction,
        PropertyDescriptor<float> distanceFadeoutFraction)
    {
        this.ColorDescriptor = color;
        this.IntensityLuxDescriptor = intensityLux;
        this.AtmosphereSlotDescriptor = atmosphereSlot;
        this.UsePerPixelAtmosphereTransmittanceDescriptor = usePerPixelAtmosphereTransmittance;
        this.CastsShadowsDescriptor = castsShadows;
        this.AffectsWorldDescriptor = affectsWorld;
        this.AngularSizeRadiansDescriptor = angularSizeRadians;
        this.ExposureCompensationDescriptor = exposureCompensation;
        this.AtmosphereDiskLuminanceScaleRgbDescriptor = atmosphereDiskLuminanceScaleRgb;
        this.ShadowBiasDescriptor = shadowBias;
        this.ShadowNormalBiasDescriptor = shadowNormalBias;
        this.ContactShadowsDescriptor = contactShadows;
        this.ShadowResolutionHintDescriptor = shadowResolutionHint;
        this.CascadeCountDescriptor = cascadeCount;
        this.SplitModeDescriptor = splitMode;
        this.MaxShadowDistanceDescriptor = maxShadowDistance;
        this.CascadeDistance0Descriptor = cascadeDistance0;
        this.CascadeDistance1Descriptor = cascadeDistance1;
        this.CascadeDistance2Descriptor = cascadeDistance2;
        this.CascadeDistance3Descriptor = cascadeDistance3;
        this.DistributionExponentDescriptor = distributionExponent;
        this.TransitionFractionDescriptor = transitionFraction;
        this.DistanceFadeoutFractionDescriptor = distanceFadeoutFraction;

        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [color.Id] = color,
            [intensityLux.Id] = intensityLux,
            [atmosphereSlot.Id] = atmosphereSlot,
            [usePerPixelAtmosphereTransmittance.Id] = usePerPixelAtmosphereTransmittance,
            [castsShadows.Id] = castsShadows,
            [affectsWorld.Id] = affectsWorld,
            [angularSizeRadians.Id] = angularSizeRadians,
            [exposureCompensation.Id] = exposureCompensation,
            [atmosphereDiskLuminanceScaleRgb.Id] = atmosphereDiskLuminanceScaleRgb,
            [shadowBias.Id] = shadowBias,
            [shadowNormalBias.Id] = shadowNormalBias,
            [contactShadows.Id] = contactShadows,
            [shadowResolutionHint.Id] = shadowResolutionHint,
            [cascadeCount.Id] = cascadeCount,
            [splitMode.Id] = splitMode,
            [maxShadowDistance.Id] = maxShadowDistance,
            [cascadeDistance0.Id] = cascadeDistance0,
            [cascadeDistance1.Id] = cascadeDistance1,
            [cascadeDistance2.Id] = cascadeDistance2,
            [cascadeDistance3.Id] = cascadeDistance3,
            [distributionExponent.Id] = distributionExponent,
            [transitionFraction.Id] = transitionFraction,
            [distanceFadeoutFraction.Id] = distanceFadeoutFraction,
        };
    }

    /// <summary>Gets the descriptor for light color.</summary>
    public PropertyDescriptor<Vector3> ColorDescriptor { get; }

    /// <summary>Gets the descriptor for intensity in lux.</summary>
    public PropertyDescriptor<float> IntensityLuxDescriptor { get; }

    /// <summary>Gets the descriptor for the atmosphere source assignment.</summary>
    public PropertyDescriptor<AtmosphereLightSlot> AtmosphereSlotDescriptor { get; }

    /// <summary>Gets the descriptor for per-pixel atmosphere transmittance.</summary>
    public PropertyDescriptor<bool> UsePerPixelAtmosphereTransmittanceDescriptor { get; }

    /// <summary>Gets the descriptor for shadow casting.</summary>
    public PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the descriptor for world lighting contribution.</summary>
    public PropertyDescriptor<bool> AffectsWorldDescriptor { get; }

    /// <summary>Gets the descriptor for angular size in radians.</summary>
    public PropertyDescriptor<float> AngularSizeRadiansDescriptor { get; }

    /// <summary>Gets the descriptor for exposure compensation.</summary>
    public PropertyDescriptor<float> ExposureCompensationDescriptor { get; }

    /// <summary>Gets the descriptor for RGB disk luminance scale.</summary>
    public PropertyDescriptor<Vector3> AtmosphereDiskLuminanceScaleRgbDescriptor { get; }

    /// <summary>Gets the descriptor for shadow depth bias.</summary>
    public PropertyDescriptor<float> ShadowBiasDescriptor { get; }

    /// <summary>Gets the descriptor for shadow normal bias.</summary>
    public PropertyDescriptor<float> ShadowNormalBiasDescriptor { get; }

    /// <summary>Gets the descriptor for contact shadows.</summary>
    public PropertyDescriptor<bool> ContactShadowsDescriptor { get; }

    /// <summary>Gets the descriptor for shadow resolution hint.</summary>
    public PropertyDescriptor<ShadowResolutionHint> ShadowResolutionHintDescriptor { get; }

    /// <summary>Gets the descriptor for CSM cascade count.</summary>
    public PropertyDescriptor<int> CascadeCountDescriptor { get; }

    /// <summary>Gets the descriptor for CSM split mode.</summary>
    public PropertyDescriptor<DirectionalCsmSplitMode> SplitModeDescriptor { get; }

    /// <summary>Gets the descriptor for max shadow distance.</summary>
    public PropertyDescriptor<float> MaxShadowDistanceDescriptor { get; }

    /// <summary>Gets the descriptor for the first cascade distance.</summary>
    public PropertyDescriptor<float> CascadeDistance0Descriptor { get; }

    /// <summary>Gets the descriptor for the second cascade distance.</summary>
    public PropertyDescriptor<float> CascadeDistance1Descriptor { get; }

    /// <summary>Gets the descriptor for the third cascade distance.</summary>
    public PropertyDescriptor<float> CascadeDistance2Descriptor { get; }

    /// <summary>Gets the descriptor for the fourth cascade distance.</summary>
    public PropertyDescriptor<float> CascadeDistance3Descriptor { get; }

    /// <summary>Gets the descriptor for generated CSM distribution.</summary>
    public PropertyDescriptor<float> DistributionExponentDescriptor { get; }

    /// <summary>Gets the descriptor for CSM transition fraction.</summary>
    public PropertyDescriptor<float> TransitionFractionDescriptor { get; }

    /// <summary>Gets the descriptor for CSM distance fadeout fraction.</summary>
    public PropertyDescriptor<float> DistanceFadeoutFractionDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical directional light descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    public static DirectionalLightDescriptors Build()
    {
        var light = BuildLightDescriptors();
        var shadow = BuildShadowDescriptors();
        var csm = BuildCsmDescriptors();
        return new(
            light.Color,
            light.IntensityLux,
            light.AtmosphereSlot,
            light.UsePerPixelAtmosphereTransmittance,
            light.CastsShadows,
            light.AffectsWorld,
            light.AngularSizeRadians,
            light.ExposureCompensation,
            light.AtmosphereDiskLuminanceScaleRgb,
            shadow.ShadowBias,
            shadow.ShadowNormalBias,
            shadow.ContactShadows,
            shadow.ShadowResolutionHint,
            csm.CascadeCount,
            csm.SplitMode,
            csm.MaxShadowDistance,
            csm.CascadeDistance0,
            csm.CascadeDistance1,
            csm.CascadeDistance2,
            csm.CascadeDistance3,
            csm.DistributionExponent,
            csm.TransitionFraction,
            csm.DistanceFadeoutFraction);
    }

    private static DirectionalLightDescriptorGroup BuildLightDescriptors()
        => new(
            BuildColorDescriptor(),
            FloatDescriptor(SceneSchema.DirectionalLight.IntensityLux, "Intensity", static light => light.IntensityLux, static (light, value) => light.IntensityLux = value, minimum: 0f),
            EnumDescriptor(SceneSchema.DirectionalLight.AtmosphereSlot, "Atmosphere source", static light => light.AtmosphereSlot, static (light, value) => light.AtmosphereSlot = value),
            BoolDescriptor(SceneSchema.DirectionalLight.UsePerPixelAtmosphereTransmittance, "Per-pixel transmittance", static light => light.UsePerPixelAtmosphereTransmittance, static (light, value) => light.UsePerPixelAtmosphereTransmittance = value),
            BoolDescriptor(SceneSchema.DirectionalLight.CastsShadows, "Shadows", static light => light.CastsShadows, static (light, value) => light.CastsShadows = value),
            BoolDescriptor(SceneSchema.DirectionalLight.AffectsWorld, "Affects World", static light => light.AffectsWorld, static (light, value) => light.AffectsWorld = value),
            FloatDescriptor(SceneSchema.DirectionalLight.AngularSizeRadians, "Angular Size", static light => light.AngularSizeRadians, static (light, value) => light.AngularSizeRadians = value, minimum: 0f, maximum: MathF.PI),
            FloatDescriptor(SceneSchema.DirectionalLight.ExposureCompensation, "Exposure Compensation", static light => light.ExposureCompensation, static (light, value) => light.ExposureCompensation = value),
            BuildDiskScaleDescriptor());

    private static DirectionalShadowDescriptorGroup BuildShadowDescriptors()
        => new(
            FloatDescriptor(SceneSchema.DirectionalLight.ShadowBias, "Shadow Bias", static light => light.ShadowBias, static (light, value) => light.ShadowBias = value, minimum: 0f),
            FloatDescriptor(SceneSchema.DirectionalLight.ShadowNormalBias, "Normal Bias", static light => light.ShadowNormalBias, static (light, value) => light.ShadowNormalBias = value, minimum: 0f),
            BoolDescriptor(SceneSchema.DirectionalLight.ContactShadows, "Contact Shadows", static light => light.ContactShadows, static (light, value) => light.ContactShadows = value),
            EnumDescriptor(SceneSchema.DirectionalLight.ShadowResolutionHint, "Resolution", static light => light.ShadowResolutionHint, static (light, value) => light.ShadowResolutionHint = value));

    private static DirectionalCsmDescriptorGroup BuildCsmDescriptors()
        => new(
            IntDescriptor(SceneSchema.DirectionalLight.CascadeCount, "Cascade Count", static light => light.CascadeCount, static (light, value) => light.CascadeCount = value),
            EnumDescriptor(SceneSchema.DirectionalLight.SplitMode, "Split Mode", static light => light.SplitMode, static (light, value) => light.SplitMode = value),
            PositiveFloatDescriptor(SceneSchema.DirectionalLight.MaxShadowDistance, "Max Distance", static light => light.MaxShadowDistance, static (light, value) => light.MaxShadowDistance = value),
            CascadeDistanceDescriptor(0, SceneSchema.DirectionalLight.CascadeDistance0, "Cascade 1"),
            CascadeDistanceDescriptor(1, SceneSchema.DirectionalLight.CascadeDistance1, "Cascade 2"),
            CascadeDistanceDescriptor(2, SceneSchema.DirectionalLight.CascadeDistance2, "Cascade 3"),
            CascadeDistanceDescriptor(3, SceneSchema.DirectionalLight.CascadeDistance3, "Cascade 4"),
            BuildDistributionExponentDescriptor(),
            FractionDescriptor(SceneSchema.DirectionalLight.TransitionFraction, "Transition", static light => light.TransitionFraction, static (light, value) => light.TransitionFraction = value),
            FractionDescriptor(SceneSchema.DirectionalLight.DistanceFadeoutFraction, "Fadeout", static light => light.DistanceFadeoutFraction, static (light, value) => light.DistanceFadeoutFraction = value));

    private static PropertyDescriptor<Vector3> BuildColorDescriptor()
        => new(
            id: SceneSchema.DirectionalLight.Color,
            reader: static target => ((DirectionalLightComponent)target).Color,
            writer: static (target, value) => ((DirectionalLightComponent)target).Color = value,
            validator: static value => !IsFinite(value)
                ? ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light values must be finite numbers.")
                : value.X < 0f || value.Y < 0f || value.Z < 0f
                    ? ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Light color must be zero or greater.")
                    : ValidationResult.Ok,
            annotation: Annotation(SceneSchema.DirectionalLight.Color.Id.JsonPointer,
                new EditorAnnotation { Group = "Light", Label = "Color", Renderer = "color-rgb" }));

    private static PropertyDescriptor<Vector3> BuildDiskScaleDescriptor()
        => new(
            id: SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb,
            reader: static target => ((DirectionalLightComponent)target).AtmosphereDiskLuminanceScaleRgb,
            writer: static (target, value) => ((DirectionalLightComponent)target).AtmosphereDiskLuminanceScaleRgb = value,
            validator: static value => IsFinite(value) && value.X >= 0 && value.Y >= 0 && value.Z >= 0
                ? ValidationResult.Ok : ValidationResult.Fail("LIGHT_DISK_SCALE_INVALID", "Disk scale must be finite and nonnegative."),
            annotation: Annotation(SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb.Id.JsonPointer, new EditorAnnotation { Group = "Atmosphere", Label = "Disk luminance scale", Renderer = "color-rgb" }));

    private static PropertyDescriptor<float> CascadeDistanceDescriptor(
        int index,
        PropertyId<float> id,
        string label)
        => PositiveFloatDescriptor(
            id,
            label,
            light => ReadCascadeDistance(light, index),
            (light, value) => WriteCascadeDistance(light, index, value));

    private static PropertyDescriptor<float> FloatDescriptor(
        PropertyId<float> id,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write,
        float minimum = float.NegativeInfinity,
        float maximum = float.PositiveInfinity)
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: value => float.IsFinite(value) && value >= minimum && value <= maximum
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, Range(label, minimum, maximum)),
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static string Range(string label, float minimum, float maximum)
        => float.IsInfinity(minimum) && float.IsInfinity(maximum) ? $"{label} must be a finite number."
            : float.IsInfinity(maximum) ? string.Create(CultureInfo.InvariantCulture, $"{label} must be {minimum} or greater.")
            : string.Create(CultureInfo.InvariantCulture, $"{label} must be between {minimum} and {maximum:0.####}.");

    private static PropertyDescriptor<float> BuildDistributionExponentDescriptor()
        => new(
            id: SceneSchema.DirectionalLight.DistributionExponent,
            reader: static target => ((DirectionalLightComponent)target).DistributionExponent,
            writer: static (target, value) => ((DirectionalLightComponent)target).DistributionExponent = value,
            validator: static value => float.IsFinite(value) && value >= 1f
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Distribution must be 1 or greater."),
            annotation: Annotation(SceneSchema.DirectionalLight.DistributionExponent.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = "Distribution", Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<float> PositiveFloatDescriptor(
        PropertyId<float> id,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write)
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => float.IsFinite(value) && value > 0f
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light values must be finite positive numbers."),
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<float> FractionDescriptor(
        PropertyId<float> id,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write)
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => float.IsFinite(value) && value is >= 0f and <= 1f
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light fractions must be finite values in [0, 1]."),
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<int> IntDescriptor(
        PropertyId<int> id,
        string label,
        Func<DirectionalLightComponent, int> read,
        Action<DirectionalLightComponent, int> write)
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => value is >= 1 and <= 4
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Cascade count must be between 1 and 4."),
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 1 }));

    private static PropertyDescriptor<T> EnumDescriptor<T>(
        PropertyId<T> id,
        string label,
        Func<DirectionalLightComponent, T> read,
        Action<DirectionalLightComponent, T> write)
        where T : struct, Enum
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light enum value is not valid."),
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "combo" }));

    private static PropertyDescriptor<bool> BoolDescriptor(
        PropertyId<bool> id,
        string label,
        Func<DirectionalLightComponent, bool> read,
        Action<DirectionalLightComponent, bool> write)
        => new(
            id: id,
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation(id.Id.JsonPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "toggle" }));

    private static EditorAnnotation Annotation(string pointer, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get(ToSceneSchemaPointer(pointer), fallback);

    private static string ToSceneSchemaPointer(string pointer)
        => pointer switch
        {
            "/color" => "#/definitions/light_common/color_rgb",
            "/intensity_lux" => "#/definitions/directional_light/intensity_lux",
            "/atmosphere_light_slot" => "#/definitions/directional_light/atmosphere_light_slot",
            "/use_per_pixel_atmosphere_transmittance" => "#/definitions/directional_light/use_per_pixel_atmosphere_transmittance",
            "/casts_shadows" => "#/definitions/light_common/casts_shadows",
            "/affects_world" => "#/definitions/light_common/affects_world",
            "/angular_size_radians" => "#/definitions/directional_light/angular_size_radians",
            "/exposure_compensation" => "#/definitions/light_common/exposure_compensation_ev",
            "/atmosphere_disk_luminance_scale_rgb" => "#/definitions/directional_light/atmosphere_disk_luminance_scale_rgb",

            "/shadow/bias" => "#/definitions/light_shadow/bias",
            "/shadow/normal_bias" => "#/definitions/light_shadow/normal_bias",
            "/shadow/contact_shadows" => "#/definitions/light_shadow/contact_shadows",
            "/shadow/resolution_hint" => "#/definitions/light_shadow/resolution_hint",
            "/csm/cascade_count" => "#/definitions/directional_light/cascade_count",
            "/csm/split_mode" => "#/definitions/directional_light/split_mode",
            "/csm/max_shadow_distance" => "#/definitions/directional_light/max_shadow_distance",
            "/csm/cascade_distances/0" => "#/definitions/directional_light/cascade_distances",
            "/csm/cascade_distances/1" => "#/definitions/directional_light/cascade_distances",
            "/csm/cascade_distances/2" => "#/definitions/directional_light/cascade_distances",
            "/csm/cascade_distances/3" => "#/definitions/directional_light/cascade_distances",
            "/csm/distribution_exponent" => "#/definitions/directional_light/distribution_exponent",
            "/csm/transition_fraction" => "#/definitions/directional_light/transition_fraction",
            "/csm/distance_fadeout_fraction" => "#/definitions/directional_light/distance_fadeout_fraction",
            _ => throw new ArgumentOutOfRangeException(nameof(pointer), pointer, "Unknown directional light property."),
        };

    private static float ReadCascadeDistance(DirectionalLightComponent light, int index)
        => index switch
        {
            0 => light.CascadeDistances.X,
            1 => light.CascadeDistances.Y,
            2 => light.CascadeDistances.Z,
            _ => light.CascadeDistances.W,
        };

    private static void WriteCascadeDistance(DirectionalLightComponent light, int index, float value)
    {
        var distances = light.CascadeDistances;
        var updated = index switch
        {
            0 => new Vector4(value, distances.Y, distances.Z, distances.W),
            1 => new Vector4(distances.X, value, distances.Z, distances.W),
            2 => new Vector4(distances.X, distances.Y, value, distances.W),
            _ => new Vector4(distances.X, distances.Y, distances.Z, value),
        };
        light.CascadeDistances = updated;
    }

    private static bool IsFinite(Vector3 value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private sealed record DirectionalLightDescriptorGroup(
        PropertyDescriptor<Vector3> Color,
        PropertyDescriptor<float> IntensityLux,
        PropertyDescriptor<AtmosphereLightSlot> AtmosphereSlot,
        PropertyDescriptor<bool> UsePerPixelAtmosphereTransmittance,
        PropertyDescriptor<bool> CastsShadows,
        PropertyDescriptor<bool> AffectsWorld,
        PropertyDescriptor<float> AngularSizeRadians,
        PropertyDescriptor<float> ExposureCompensation,
        PropertyDescriptor<Vector3> AtmosphereDiskLuminanceScaleRgb);

    private sealed record DirectionalShadowDescriptorGroup(
        PropertyDescriptor<float> ShadowBias,
        PropertyDescriptor<float> ShadowNormalBias,
        PropertyDescriptor<bool> ContactShadows,
        PropertyDescriptor<ShadowResolutionHint> ShadowResolutionHint);

    private sealed record DirectionalCsmDescriptorGroup(
        PropertyDescriptor<int> CascadeCount,
        PropertyDescriptor<DirectionalCsmSplitMode> SplitMode,
        PropertyDescriptor<float> MaxShadowDistance,
        PropertyDescriptor<float> CascadeDistance0,
        PropertyDescriptor<float> CascadeDistance1,
        PropertyDescriptor<float> CascadeDistance2,
        PropertyDescriptor<float> CascadeDistance3,
        PropertyDescriptor<float> DistributionExponent,
        PropertyDescriptor<float> TransitionFraction,
        PropertyDescriptor<float> DistanceFadeoutFraction);
}
