// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for directional light inspector properties.
/// </summary>
internal sealed class DirectionalLightDescriptors
{
    private DirectionalLightDescriptors(
        PropertyDescriptor<Vector3> color,
        PropertyDescriptor<float> intensityLux,
        PropertyDescriptor<bool> isSunLight,
        PropertyDescriptor<bool> environmentContribution,
        PropertyDescriptor<bool> castsShadows,
        PropertyDescriptor<bool> affectsWorld,
        PropertyDescriptor<float> angularSizeRadians,
        PropertyDescriptor<float> exposureCompensation,
        PropertyDescriptor<LightMobility> mobility,
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
        this.IsSunLightDescriptor = isSunLight;
        this.EnvironmentContributionDescriptor = environmentContribution;
        this.CastsShadowsDescriptor = castsShadows;
        this.AffectsWorldDescriptor = affectsWorld;
        this.AngularSizeRadiansDescriptor = angularSizeRadians;
        this.ExposureCompensationDescriptor = exposureCompensation;
        this.MobilityDescriptor = mobility;
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
            [isSunLight.Id] = isSunLight,
            [environmentContribution.Id] = environmentContribution,
            [castsShadows.Id] = castsShadows,
            [affectsWorld.Id] = affectsWorld,
            [angularSizeRadians.Id] = angularSizeRadians,
            [exposureCompensation.Id] = exposureCompensation,
            [mobility.Id] = mobility,
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

    /// <summary>Gets the typed property id for light color.</summary>
    internal PropertyId<Vector3> Color => new(this.ColorDescriptor.Id);

    /// <summary>Gets the typed property id for intensity in lux.</summary>
    internal PropertyId<float> IntensityLux => new(this.IntensityLuxDescriptor.Id);

    /// <summary>Gets the typed property id for the sun-light flag.</summary>
    internal PropertyId<bool> IsSunLight => new(this.IsSunLightDescriptor.Id);

    /// <summary>Gets the typed property id for environment contribution.</summary>
    internal PropertyId<bool> EnvironmentContribution => new(this.EnvironmentContributionDescriptor.Id);

    /// <summary>Gets the typed property id for shadow casting.</summary>
    internal PropertyId<bool> CastsShadows => new(this.CastsShadowsDescriptor.Id);

    /// <summary>Gets the typed property id for world lighting contribution.</summary>
    internal PropertyId<bool> AffectsWorld => new(this.AffectsWorldDescriptor.Id);

    /// <summary>Gets the typed property id for angular size in radians.</summary>
    internal PropertyId<float> AngularSizeRadians => new(this.AngularSizeRadiansDescriptor.Id);

    /// <summary>Gets the typed property id for exposure compensation.</summary>
    internal PropertyId<float> ExposureCompensation => new(this.ExposureCompensationDescriptor.Id);

    /// <summary>Gets the typed property id for light mobility.</summary>
    internal PropertyId<LightMobility> Mobility => new(this.MobilityDescriptor.Id);

    /// <summary>Gets the typed property id for shadow depth bias.</summary>
    internal PropertyId<float> ShadowBias => new(this.ShadowBiasDescriptor.Id);

    /// <summary>Gets the typed property id for shadow normal bias.</summary>
    internal PropertyId<float> ShadowNormalBias => new(this.ShadowNormalBiasDescriptor.Id);

    /// <summary>Gets the typed property id for contact shadows.</summary>
    internal PropertyId<bool> ContactShadows => new(this.ContactShadowsDescriptor.Id);

    /// <summary>Gets the typed property id for shadow resolution hint.</summary>
    internal PropertyId<ShadowResolutionHint> ShadowResolutionHint => new(this.ShadowResolutionHintDescriptor.Id);

    /// <summary>Gets the typed property id for CSM cascade count.</summary>
    internal PropertyId<int> CascadeCount => new(this.CascadeCountDescriptor.Id);

    /// <summary>Gets the typed property id for CSM split mode.</summary>
    internal PropertyId<DirectionalCsmSplitMode> SplitMode => new(this.SplitModeDescriptor.Id);

    /// <summary>Gets the typed property id for max shadow distance.</summary>
    internal PropertyId<float> MaxShadowDistance => new(this.MaxShadowDistanceDescriptor.Id);

    /// <summary>Gets the typed property id for the first cascade distance.</summary>
    internal PropertyId<float> CascadeDistance0 => new(this.CascadeDistance0Descriptor.Id);

    /// <summary>Gets the typed property id for the second cascade distance.</summary>
    internal PropertyId<float> CascadeDistance1 => new(this.CascadeDistance1Descriptor.Id);

    /// <summary>Gets the typed property id for the third cascade distance.</summary>
    internal PropertyId<float> CascadeDistance2 => new(this.CascadeDistance2Descriptor.Id);

    /// <summary>Gets the typed property id for the fourth cascade distance.</summary>
    internal PropertyId<float> CascadeDistance3 => new(this.CascadeDistance3Descriptor.Id);

    /// <summary>Gets the typed property id for generated CSM distribution.</summary>
    internal PropertyId<float> DistributionExponent => new(this.DistributionExponentDescriptor.Id);

    /// <summary>Gets the typed property id for CSM transition fraction.</summary>
    internal PropertyId<float> TransitionFraction => new(this.TransitionFractionDescriptor.Id);

    /// <summary>Gets the typed property id for CSM distance fadeout fraction.</summary>
    internal PropertyId<float> DistanceFadeoutFraction => new(this.DistanceFadeoutFractionDescriptor.Id);

    /// <summary>Gets the descriptor for light color.</summary>
    internal PropertyDescriptor<Vector3> ColorDescriptor { get; }

    /// <summary>Gets the descriptor for intensity in lux.</summary>
    internal PropertyDescriptor<float> IntensityLuxDescriptor { get; }

    /// <summary>Gets the descriptor for the sun-light flag.</summary>
    internal PropertyDescriptor<bool> IsSunLightDescriptor { get; }

    /// <summary>Gets the descriptor for environment contribution.</summary>
    internal PropertyDescriptor<bool> EnvironmentContributionDescriptor { get; }

    /// <summary>Gets the descriptor for shadow casting.</summary>
    internal PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the descriptor for world lighting contribution.</summary>
    internal PropertyDescriptor<bool> AffectsWorldDescriptor { get; }

    /// <summary>Gets the descriptor for angular size in radians.</summary>
    internal PropertyDescriptor<float> AngularSizeRadiansDescriptor { get; }

    /// <summary>Gets the descriptor for exposure compensation.</summary>
    internal PropertyDescriptor<float> ExposureCompensationDescriptor { get; }

    /// <summary>Gets the descriptor for light mobility.</summary>
    internal PropertyDescriptor<LightMobility> MobilityDescriptor { get; }

    /// <summary>Gets the descriptor for shadow depth bias.</summary>
    internal PropertyDescriptor<float> ShadowBiasDescriptor { get; }

    /// <summary>Gets the descriptor for shadow normal bias.</summary>
    internal PropertyDescriptor<float> ShadowNormalBiasDescriptor { get; }

    /// <summary>Gets the descriptor for contact shadows.</summary>
    internal PropertyDescriptor<bool> ContactShadowsDescriptor { get; }

    /// <summary>Gets the descriptor for shadow resolution hint.</summary>
    internal PropertyDescriptor<ShadowResolutionHint> ShadowResolutionHintDescriptor { get; }

    /// <summary>Gets the descriptor for CSM cascade count.</summary>
    internal PropertyDescriptor<int> CascadeCountDescriptor { get; }

    /// <summary>Gets the descriptor for CSM split mode.</summary>
    internal PropertyDescriptor<DirectionalCsmSplitMode> SplitModeDescriptor { get; }

    /// <summary>Gets the descriptor for max shadow distance.</summary>
    internal PropertyDescriptor<float> MaxShadowDistanceDescriptor { get; }

    /// <summary>Gets the descriptor for the first cascade distance.</summary>
    internal PropertyDescriptor<float> CascadeDistance0Descriptor { get; }

    /// <summary>Gets the descriptor for the second cascade distance.</summary>
    internal PropertyDescriptor<float> CascadeDistance1Descriptor { get; }

    /// <summary>Gets the descriptor for the third cascade distance.</summary>
    internal PropertyDescriptor<float> CascadeDistance2Descriptor { get; }

    /// <summary>Gets the descriptor for the fourth cascade distance.</summary>
    internal PropertyDescriptor<float> CascadeDistance3Descriptor { get; }

    /// <summary>Gets the descriptor for generated CSM distribution.</summary>
    internal PropertyDescriptor<float> DistributionExponentDescriptor { get; }

    /// <summary>Gets the descriptor for CSM transition fraction.</summary>
    internal PropertyDescriptor<float> TransitionFractionDescriptor { get; }

    /// <summary>Gets the descriptor for CSM distance fadeout fraction.</summary>
    internal PropertyDescriptor<float> DistanceFadeoutFractionDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical directional light descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    internal static DirectionalLightDescriptors Build()
    {
        var light = BuildLightDescriptors();
        var shadow = BuildShadowDescriptors();
        var csm = BuildCsmDescriptors();
        return new(
            light.Color,
            light.IntensityLux,
            light.IsSunLight,
            light.EnvironmentContribution,
            light.CastsShadows,
            light.AffectsWorld,
            light.AngularSizeRadians,
            light.ExposureCompensation,
            light.Mobility,
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
            FloatDescriptor("/intensity_lux", "Intensity", static light => light.IntensityLux, static (light, value) => light.IntensityLux = Math.Max(0f, value), "directional_light.intensity_lux"),
            BoolDescriptor("/is_sun_light", "Sun", static light => light.IsSunLight, static (light, value) => light.IsSunLight = value, "directional_light.is_sun_light"),
            BoolDescriptor("/environment_contribution", "Environment", static light => light.EnvironmentContribution, static (light, value) => light.EnvironmentContribution = value, "directional_light.environment_contribution"),
            BoolDescriptor("/casts_shadows", "Shadows", static light => light.CastsShadows, static (light, value) => light.CastsShadows = value, "directional_light.casts_shadows"),
            BoolDescriptor("/affects_world", "Affects World", static light => light.AffectsWorld, static (light, value) => light.AffectsWorld = value, "directional_light.affects_world"),
            FloatDescriptor("/angular_size_radians", "Angular Size", static light => light.AngularSizeRadians, static (light, value) => light.AngularSizeRadians = Math.Max(0f, value), "directional_light.angular_size_radians"),
            FloatDescriptor("/exposure_compensation", "Exposure Compensation", static light => light.ExposureCompensation, static (light, value) => light.ExposureCompensation = Math.Clamp(value, -10f, 10f), "directional_light.exposure_compensation"),
            EnumDescriptor("/mobility", "Mobility", static light => light.Mobility, static (light, value) => light.Mobility = value, "directional_light.mobility"));

    private static DirectionalShadowDescriptorGroup BuildShadowDescriptors()
        => new(
            FloatDescriptor("/shadow/bias", "Shadow Bias", static light => light.ShadowBias, static (light, value) => light.ShadowBias = value, "directional_light.shadow.bias"),
            FloatDescriptor("/shadow/normal_bias", "Normal Bias", static light => light.ShadowNormalBias, static (light, value) => light.ShadowNormalBias = Math.Max(0f, value), "directional_light.shadow.normal_bias"),
            BoolDescriptor("/shadow/contact_shadows", "Contact Shadows", static light => light.ContactShadows, static (light, value) => light.ContactShadows = value, "directional_light.shadow.contact_shadows"),
            EnumDescriptor("/shadow/resolution_hint", "Resolution", static light => light.ShadowResolutionHint, static (light, value) => light.ShadowResolutionHint = value, "directional_light.shadow.resolution_hint"));

    private static DirectionalCsmDescriptorGroup BuildCsmDescriptors()
        => new(
            IntDescriptor("/csm/cascade_count", "Cascade Count", static light => light.CascadeCount, static (light, value) => light.CascadeCount = Math.Clamp(value, 1, 4), "directional_light.cascade_count"),
            EnumDescriptor("/csm/split_mode", "Split Mode", static light => light.SplitMode, static (light, value) => light.SplitMode = value, "directional_light.split_mode"),
            PositiveFloatDescriptor("/csm/max_shadow_distance", "Max Distance", static light => light.MaxShadowDistance, static (light, value) => light.MaxShadowDistance = Math.Max(0.001f, value), "directional_light.max_shadow_distance"),
            CascadeDistanceDescriptor(0, "/csm/cascade_distances/0", "Cascade 1", "directional_light.cascade_distance_0"),
            CascadeDistanceDescriptor(1, "/csm/cascade_distances/1", "Cascade 2", "directional_light.cascade_distance_1"),
            CascadeDistanceDescriptor(2, "/csm/cascade_distances/2", "Cascade 3", "directional_light.cascade_distance_2"),
            CascadeDistanceDescriptor(3, "/csm/cascade_distances/3", "Cascade 4", "directional_light.cascade_distance_3"),
            PositiveFloatDescriptor("/csm/distribution_exponent", "Distribution", static light => light.DistributionExponent, static (light, value) => light.DistributionExponent = Math.Max(1f, value), "directional_light.distribution_exponent"),
            FractionDescriptor("/csm/transition_fraction", "Transition", static light => light.TransitionFraction, static (light, value) => light.TransitionFraction = Math.Clamp(value, 0f, 1f), "directional_light.transition_fraction"),
            FractionDescriptor("/csm/distance_fadeout_fraction", "Fadeout", static light => light.DistanceFadeoutFraction, static (light, value) => light.DistanceFadeoutFraction = Math.Clamp(value, 0f, 1f), "directional_light.distance_fadeout_fraction"));

    private static PropertyDescriptor<Vector3> BuildColorDescriptor()
        => new(
            id: new PropertyId<Vector3>(SceneDocumentCommandService.DirectionalLightKind, "/color"),
            reader: static target => ((DirectionalLightComponent)target).Color,
            writer: static (target, value) => ((DirectionalLightComponent)target).Color = Vector3.Clamp(value, Vector3.Zero, Vector3.One),
            validator: static value => IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light values must be finite numbers."),
            annotation: Annotation(
                "directional_light.color",
                new EditorAnnotation { Group = "Light", Label = "Color", Renderer = "color-rgb" }),
            engineCommandKey: "directional_light.color");

    private static PropertyDescriptor<float> CascadeDistanceDescriptor(
        int index,
        string pointer,
        string label,
        string engineCommandKey)
        => PositiveFloatDescriptor(
            pointer,
            label,
            light => ReadCascadeDistance(light, index),
            (light, value) => WriteCascadeDistance(light, index, value),
            engineCommandKey);

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => float.IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light values must be finite numbers."),
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<float> PositiveFloatDescriptor(
        string pointer,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => float.IsFinite(value) && value > 0f
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light values must be finite positive numbers."),
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<float> FractionDescriptor(
        string pointer,
        string label,
        Func<DirectionalLightComponent, float> read,
        Action<DirectionalLightComponent, float> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => float.IsFinite(value) && value is >= 0f and <= 1f
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light fractions must be finite values in [0, 1]."),
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<int> IntDescriptor(
        string pointer,
        string label,
        Func<DirectionalLightComponent, int> read,
        Action<DirectionalLightComponent, int> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<int>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => value is >= 1 and <= 4
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Cascade count must be between 1 and 4."),
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 1 }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<T> EnumDescriptor<T>(
        string pointer,
        string label,
        Func<DirectionalLightComponent, T> read,
        Action<DirectionalLightComponent, T> write,
        string engineCommandKey)
        where T : struct, Enum
        => new(
            id: new PropertyId<T>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.DirectionalLightFieldNotFinite, "Directional light enum value is not valid."),
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "combo" }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<bool> BoolDescriptor(
        string pointer,
        string label,
        Func<DirectionalLightComponent, bool> read,
        Action<DirectionalLightComponent, bool> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<bool>(SceneDocumentCommandService.DirectionalLightKind, pointer),
            reader: target => read((DirectionalLightComponent)target),
            writer: (target, value) => write((DirectionalLightComponent)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation(engineCommandKey, new EditorAnnotation { Group = "Light", Label = label, Renderer = "toggle" }),
            engineCommandKey: engineCommandKey);

    private static EditorAnnotation Annotation(string engineCommandKey, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get(ToSceneSchemaPointer(engineCommandKey), fallback);

    private static string ToSceneSchemaPointer(string engineCommandKey)
        => engineCommandKey switch
        {
            "directional_light.color" => "#/definitions/light_common/color_rgb",
            "directional_light.intensity_lux" => "#/definitions/directional_light/intensity_lux",
            "directional_light.is_sun_light" => "#/definitions/directional_light/is_sun_light",
            "directional_light.environment_contribution" => "#/definitions/directional_light/environment_contribution",
            "directional_light.casts_shadows" => "#/definitions/light_common/casts_shadows",
            "directional_light.affects_world" => "#/definitions/light_common/affects_world",
            "directional_light.angular_size_radians" => "#/definitions/directional_light/angular_size_radians",
            "directional_light.exposure_compensation" => "#/definitions/light_common/exposure_compensation_ev",
            "directional_light.mobility" => "#/definitions/light_common/mobility",
            "directional_light.shadow.bias" => "#/definitions/light_shadow/bias",
            "directional_light.shadow.normal_bias" => "#/definitions/light_shadow/normal_bias",
            "directional_light.shadow.contact_shadows" => "#/definitions/light_shadow/contact_shadows",
            "directional_light.shadow.resolution_hint" => "#/definitions/light_shadow/resolution_hint",
            "directional_light.cascade_count" => "#/definitions/directional_light/cascade_count",
            "directional_light.split_mode" => "#/definitions/directional_light/split_mode",
            "directional_light.max_shadow_distance" => "#/definitions/directional_light/max_shadow_distance",
            "directional_light.cascade_distance_0" => "#/definitions/directional_light/cascade_distances",
            "directional_light.cascade_distance_1" => "#/definitions/directional_light/cascade_distances",
            "directional_light.cascade_distance_2" => "#/definitions/directional_light/cascade_distances",
            "directional_light.cascade_distance_3" => "#/definitions/directional_light/cascade_distances",
            "directional_light.distribution_exponent" => "#/definitions/directional_light/distribution_exponent",
            "directional_light.transition_fraction" => "#/definitions/directional_light/transition_fraction",
            "directional_light.distance_fadeout_fraction" => "#/definitions/directional_light/distance_fadeout_fraction",
            _ => throw new ArgumentOutOfRangeException(nameof(engineCommandKey), engineCommandKey, "Unknown directional light command key."),
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
            0 => new Vector4(Math.Max(0.001f, value), distances.Y, distances.Z, distances.W),
            1 => new Vector4(distances.X, Math.Max(0.001f, value), distances.Z, distances.W),
            2 => new Vector4(distances.X, distances.Y, Math.Max(0.001f, value), distances.W),
            _ => new Vector4(distances.X, distances.Y, distances.Z, Math.Max(0.001f, value)),
        };
        light.CascadeDistances = updated;
    }

    private static bool IsFinite(Vector3 value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private sealed record DirectionalLightDescriptorGroup(
        PropertyDescriptor<Vector3> Color,
        PropertyDescriptor<float> IntensityLux,
        PropertyDescriptor<bool> IsSunLight,
        PropertyDescriptor<bool> EnvironmentContribution,
        PropertyDescriptor<bool> CastsShadows,
        PropertyDescriptor<bool> AffectsWorld,
        PropertyDescriptor<float> AngularSizeRadians,
        PropertyDescriptor<float> ExposureCompensation,
        PropertyDescriptor<LightMobility> Mobility);

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
