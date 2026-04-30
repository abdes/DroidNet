// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for scene environment inspector properties.
/// </summary>
internal sealed class SceneEnvironmentDescriptors
{
    private readonly List<PropertyDescriptor> all;
    private readonly PropertyDescriptor<bool> atmosphereEnabledDescriptor;
    private readonly PropertyDescriptor<Guid?> sunNodeIdDescriptor;
    private readonly PropertyDescriptor<Vector3> backgroundColorDescriptor;
    private readonly PropertyDescriptor<ExposureMode> exposureModeDescriptor;
    private readonly PropertyDescriptor<bool> exposureEnabledDescriptor;
    private readonly PropertyDescriptor<float> manualExposureEvDescriptor;
    private readonly PropertyDescriptor<float> exposureCompensationDescriptor;
    private readonly PropertyDescriptor<float> exposureKeyDescriptor;
    private readonly PropertyDescriptor<ToneMappingMode> toneMappingDescriptor;
    private readonly PropertyDescriptor<MeteringMode> autoExposureMeteringModeDescriptor;
    private readonly PropertyDescriptor<float> autoExposureMinEvDescriptor;
    private readonly PropertyDescriptor<float> autoExposureMaxEvDescriptor;
    private readonly PropertyDescriptor<float> autoExposureSpeedUpDescriptor;
    private readonly PropertyDescriptor<float> autoExposureSpeedDownDescriptor;
    private readonly PropertyDescriptor<float> autoExposureLowPercentileDescriptor;
    private readonly PropertyDescriptor<float> autoExposureHighPercentileDescriptor;
    private readonly PropertyDescriptor<float> autoExposureMinLogLuminanceDescriptor;
    private readonly PropertyDescriptor<float> autoExposureLogLuminanceRangeDescriptor;
    private readonly PropertyDescriptor<float> autoExposureTargetLuminanceDescriptor;
    private readonly PropertyDescriptor<float> autoExposureSpotMeterRadiusDescriptor;
    private readonly PropertyDescriptor<float> bloomIntensityDescriptor;
    private readonly PropertyDescriptor<float> bloomThresholdDescriptor;
    private readonly PropertyDescriptor<float> saturationDescriptor;
    private readonly PropertyDescriptor<float> contrastDescriptor;
    private readonly PropertyDescriptor<float> vignetteIntensityDescriptor;
    private readonly PropertyDescriptor<float> displayGammaDescriptor;
    private readonly PropertyDescriptor<float> planetRadiusMetersDescriptor;
    private readonly PropertyDescriptor<float> atmosphereHeightMetersDescriptor;
    private readonly PropertyDescriptor<Vector3> groundAlbedoDescriptor;
    private readonly PropertyDescriptor<float> rayleighScaleHeightMetersDescriptor;
    private readonly PropertyDescriptor<float> mieScaleHeightMetersDescriptor;
    private readonly PropertyDescriptor<float> mieAnisotropyDescriptor;
    private readonly PropertyDescriptor<Vector3> skyLuminanceDescriptor;
    private readonly PropertyDescriptor<float> aerialPerspectiveDistanceScaleDescriptor;
    private readonly PropertyDescriptor<float> aerialScatteringStrengthDescriptor;
    private readonly PropertyDescriptor<float> aerialPerspectiveStartDepthMetersDescriptor;
    private readonly PropertyDescriptor<float> heightFogContributionDescriptor;
    private readonly PropertyDescriptor<bool> sunDiskEnabledDescriptor;

    private SceneEnvironmentDescriptors(IReadOnlyList<PropertyDescriptor> descriptors)
    {
        this.all = [.. descriptors];
        this.ById = this.all.ToDictionary(static descriptor => descriptor.Id);
        this.atmosphereEnabledDescriptor = this.Get<bool>("/atmosphere_enabled");
        this.sunNodeIdDescriptor = this.Get<Guid?>("/sun_node_id");
        this.backgroundColorDescriptor = this.Get<Vector3>("/background_color");
        this.exposureModeDescriptor = this.Get<ExposureMode>("/post_process/exposure_mode");
        this.exposureEnabledDescriptor = this.Get<bool>("/post_process/exposure_enabled");
        this.manualExposureEvDescriptor = this.Get<float>("/post_process/manual_exposure_ev");
        this.exposureCompensationDescriptor = this.Get<float>("/post_process/exposure_compensation_ev");
        this.exposureKeyDescriptor = this.Get<float>("/post_process/exposure_key");
        this.toneMappingDescriptor = this.Get<ToneMappingMode>("/post_process/tone_mapper");
        this.autoExposureMeteringModeDescriptor = this.Get<MeteringMode>("/post_process/auto_exposure_metering_mode");
        this.autoExposureMinEvDescriptor = this.Get<float>("/post_process/auto_exposure_min_ev");
        this.autoExposureMaxEvDescriptor = this.Get<float>("/post_process/auto_exposure_max_ev");
        this.autoExposureSpeedUpDescriptor = this.Get<float>("/post_process/auto_exposure_speed_up");
        this.autoExposureSpeedDownDescriptor = this.Get<float>("/post_process/auto_exposure_speed_down");
        this.autoExposureLowPercentileDescriptor = this.Get<float>("/post_process/auto_exposure_low_percentile");
        this.autoExposureHighPercentileDescriptor = this.Get<float>("/post_process/auto_exposure_high_percentile");
        this.autoExposureMinLogLuminanceDescriptor = this.Get<float>("/post_process/auto_exposure_min_log_luminance");
        this.autoExposureLogLuminanceRangeDescriptor = this.Get<float>("/post_process/auto_exposure_log_luminance_range");
        this.autoExposureTargetLuminanceDescriptor = this.Get<float>("/post_process/auto_exposure_target_luminance");
        this.autoExposureSpotMeterRadiusDescriptor = this.Get<float>("/post_process/auto_exposure_spot_meter_radius");
        this.bloomIntensityDescriptor = this.Get<float>("/post_process/bloom_intensity");
        this.bloomThresholdDescriptor = this.Get<float>("/post_process/bloom_threshold");
        this.saturationDescriptor = this.Get<float>("/post_process/saturation");
        this.contrastDescriptor = this.Get<float>("/post_process/contrast");
        this.vignetteIntensityDescriptor = this.Get<float>("/post_process/vignette_intensity");
        this.displayGammaDescriptor = this.Get<float>("/post_process/display_gamma");
        this.planetRadiusMetersDescriptor = this.Get<float>("/sky_atmosphere/planet_radius_meters");
        this.atmosphereHeightMetersDescriptor = this.Get<float>("/sky_atmosphere/atmosphere_height_meters");
        this.groundAlbedoDescriptor = this.Get<Vector3>("/sky_atmosphere/ground_albedo_rgb");
        this.rayleighScaleHeightMetersDescriptor = this.Get<float>("/sky_atmosphere/rayleigh_scale_height_meters");
        this.mieScaleHeightMetersDescriptor = this.Get<float>("/sky_atmosphere/mie_scale_height_meters");
        this.mieAnisotropyDescriptor = this.Get<float>("/sky_atmosphere/mie_anisotropy");
        this.skyLuminanceDescriptor = this.Get<Vector3>("/sky_atmosphere/sky_luminance_factor_rgb");
        this.aerialPerspectiveDistanceScaleDescriptor = this.Get<float>("/sky_atmosphere/aerial_perspective_distance_scale");
        this.aerialScatteringStrengthDescriptor = this.Get<float>("/sky_atmosphere/aerial_scattering_strength");
        this.aerialPerspectiveStartDepthMetersDescriptor = this.Get<float>("/sky_atmosphere/aerial_perspective_start_depth_meters");
        this.heightFogContributionDescriptor = this.Get<float>("/sky_atmosphere/height_fog_contribution");
        this.sunDiskEnabledDescriptor = this.Get<bool>("/sky_atmosphere/sun_disk_enabled");
    }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Gets the typed property id for atmosphere enablement.</summary>
    internal PropertyId<bool> AtmosphereEnabled => this.atmosphereEnabledDescriptor.TypedId;

    /// <summary>Gets the typed property id for the environment sun node.</summary>
    internal PropertyId<Guid?> SunNodeId => this.sunNodeIdDescriptor.TypedId;

    /// <summary>Gets the typed property id for the background color.</summary>
    internal PropertyId<Vector3> BackgroundColor => this.backgroundColorDescriptor.TypedId;

    /// <summary>Gets the typed property id for exposure mode.</summary>
    internal PropertyId<ExposureMode> ExposureMode => this.exposureModeDescriptor.TypedId;

    /// <summary>Gets the typed property id for post-process exposure enablement.</summary>
    internal PropertyId<bool> ExposureEnabled => this.exposureEnabledDescriptor.TypedId;

    /// <summary>Gets the typed property id for manual exposure EV.</summary>
    internal PropertyId<float> ManualExposureEv => this.manualExposureEvDescriptor.TypedId;

    /// <summary>Gets the typed property id for exposure compensation.</summary>
    internal PropertyId<float> ExposureCompensation => this.exposureCompensationDescriptor.TypedId;

    /// <summary>Gets the typed property id for exposure key.</summary>
    internal PropertyId<float> ExposureKey => this.exposureKeyDescriptor.TypedId;

    /// <summary>Gets the typed property id for tone mapping.</summary>
    internal PropertyId<ToneMappingMode> ToneMapping => this.toneMappingDescriptor.TypedId;

    /// <summary>Gets the typed property id for auto exposure metering mode.</summary>
    internal PropertyId<MeteringMode> AutoExposureMeteringMode => this.autoExposureMeteringModeDescriptor.TypedId;

    /// <summary>Gets the typed property id for auto exposure minimum EV.</summary>
    internal PropertyId<float> AutoExposureMinEv => this.autoExposureMinEvDescriptor.TypedId;

    /// <summary>Gets the typed property id for auto exposure maximum EV.</summary>
    internal PropertyId<float> AutoExposureMaxEv => this.autoExposureMaxEvDescriptor.TypedId;

    /// <summary>Gets the typed property id for auto exposure adaptation speed up.</summary>
    internal PropertyId<float> AutoExposureSpeedUp => this.autoExposureSpeedUpDescriptor.TypedId;

    /// <summary>Gets the typed property id for auto exposure adaptation speed down.</summary>
    internal PropertyId<float> AutoExposureSpeedDown => this.autoExposureSpeedDownDescriptor.TypedId;

    /// <summary>Gets the typed property id for the low exposure percentile.</summary>
    internal PropertyId<float> AutoExposureLowPercentile => this.autoExposureLowPercentileDescriptor.TypedId;

    /// <summary>Gets the typed property id for the high exposure percentile.</summary>
    internal PropertyId<float> AutoExposureHighPercentile => this.autoExposureHighPercentileDescriptor.TypedId;

    /// <summary>Gets the typed property id for minimum log luminance.</summary>
    internal PropertyId<float> AutoExposureMinLogLuminance => this.autoExposureMinLogLuminanceDescriptor.TypedId;

    /// <summary>Gets the typed property id for log luminance range.</summary>
    internal PropertyId<float> AutoExposureLogLuminanceRange => this.autoExposureLogLuminanceRangeDescriptor.TypedId;

    /// <summary>Gets the typed property id for target luminance.</summary>
    internal PropertyId<float> AutoExposureTargetLuminance => this.autoExposureTargetLuminanceDescriptor.TypedId;

    /// <summary>Gets the typed property id for spot meter radius.</summary>
    internal PropertyId<float> AutoExposureSpotMeterRadius => this.autoExposureSpotMeterRadiusDescriptor.TypedId;

    /// <summary>Gets the typed property id for bloom intensity.</summary>
    internal PropertyId<float> BloomIntensity => this.bloomIntensityDescriptor.TypedId;

    /// <summary>Gets the typed property id for bloom threshold.</summary>
    internal PropertyId<float> BloomThreshold => this.bloomThresholdDescriptor.TypedId;

    /// <summary>Gets the typed property id for color saturation.</summary>
    internal PropertyId<float> Saturation => this.saturationDescriptor.TypedId;

    /// <summary>Gets the typed property id for contrast.</summary>
    internal PropertyId<float> Contrast => this.contrastDescriptor.TypedId;

    /// <summary>Gets the typed property id for vignette intensity.</summary>
    internal PropertyId<float> VignetteIntensity => this.vignetteIntensityDescriptor.TypedId;

    /// <summary>Gets the typed property id for display gamma.</summary>
    internal PropertyId<float> DisplayGamma => this.displayGammaDescriptor.TypedId;

    /// <summary>Gets the typed property id for planet radius in meters.</summary>
    internal PropertyId<float> PlanetRadiusMeters => this.planetRadiusMetersDescriptor.TypedId;

    /// <summary>Gets the typed property id for atmosphere height in meters.</summary>
    internal PropertyId<float> AtmosphereHeightMeters => this.atmosphereHeightMetersDescriptor.TypedId;

    /// <summary>Gets the typed property id for ground albedo.</summary>
    internal PropertyId<Vector3> GroundAlbedo => this.groundAlbedoDescriptor.TypedId;

    /// <summary>Gets the typed property id for Rayleigh scale height in meters.</summary>
    internal PropertyId<float> RayleighScaleHeightMeters => this.rayleighScaleHeightMetersDescriptor.TypedId;

    /// <summary>Gets the typed property id for Mie scale height in meters.</summary>
    internal PropertyId<float> MieScaleHeightMeters => this.mieScaleHeightMetersDescriptor.TypedId;

    /// <summary>Gets the typed property id for Mie anisotropy.</summary>
    internal PropertyId<float> MieAnisotropy => this.mieAnisotropyDescriptor.TypedId;

    /// <summary>Gets the typed property id for sky luminance factors.</summary>
    internal PropertyId<Vector3> SkyLuminance => this.skyLuminanceDescriptor.TypedId;

    /// <summary>Gets the typed property id for aerial perspective distance scale.</summary>
    internal PropertyId<float> AerialPerspectiveDistanceScale => this.aerialPerspectiveDistanceScaleDescriptor.TypedId;

    /// <summary>Gets the typed property id for aerial scattering strength.</summary>
    internal PropertyId<float> AerialScatteringStrength => this.aerialScatteringStrengthDescriptor.TypedId;

    /// <summary>Gets the typed property id for aerial perspective start depth in meters.</summary>
    internal PropertyId<float> AerialPerspectiveStartDepthMeters => this.aerialPerspectiveStartDepthMetersDescriptor.TypedId;

    /// <summary>Gets the typed property id for height fog contribution.</summary>
    internal PropertyId<float> HeightFogContribution => this.heightFogContributionDescriptor.TypedId;

    /// <summary>Gets the typed property id for sun disk rendering.</summary>
    internal PropertyId<bool> SunDiskEnabled => this.sunDiskEnabledDescriptor.TypedId;

    /// <summary>
    /// Builds the canonical scene environment descriptor catalog.
    /// </summary>
    /// <returns>The descriptor catalog.</returns>
    internal static SceneEnvironmentDescriptors Build()
    {
        var descriptors = new List<PropertyDescriptor>();
        AddRootDescriptors(descriptors);
        AddExposureDescriptors(descriptors);
        AddPostProcessFloatDescriptors(descriptors);
        AddSkyAtmosphereDescriptors(descriptors);
        return new SceneEnvironmentDescriptors(descriptors);
    }

    private static void AddRootDescriptors(List<PropertyDescriptor> descriptors)
    {
        descriptors.Add(BoolDescriptor("/atmosphere_enabled", "Enabled", static value => value.AtmosphereEnabled, static (value, next) => value with { AtmosphereEnabled = next }, "environment.atmosphere_enabled"));
        descriptors.Add(NullableGuidDescriptor("/sun_node_id", "Sun Light", static value => value.SunNodeId, static (value, next) => value with { SunNodeId = next }, "environment.sun_node_id"));
        descriptors.Add(VectorDescriptor(
            "/background_color",
            "Background",
            static value => value.BackgroundColor,
            static (value, next) => value with { BackgroundColor = next },
            SceneDiagnosticCodes.EnvironmentBackgroundColorInvalid,
            "Background color values must be finite.",
            "environment.background_color"));
    }

    private static void AddExposureDescriptors(List<PropertyDescriptor> descriptors)
    {
        descriptors.Add(EnumDescriptor("/post_process/exposure_mode", "Mode", static value => value.PostProcess.ExposureMode, WriteExposureMode, SceneDiagnosticCodes.EnvironmentExposureModeInvalid, "Exposure mode is not valid.", "environment.post_process.exposure_mode"));
        descriptors.Add(BoolDescriptor("/post_process/exposure_enabled", "Enabled", static value => value.PostProcess.ExposureEnabled, static (value, next) => value with { PostProcess = value.PostProcess with { ExposureEnabled = next } }, "environment.post_process.exposure_enabled"));
        descriptors.Add(FloatDescriptor("/post_process/manual_exposure_ev", "Manual EV", static value => value.PostProcess.ManualExposureEv, WriteManualExposure, SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Manual exposure must be finite.", "environment.post_process.manual_exposure_ev"));
        descriptors.Add(FloatDescriptor("/post_process/exposure_compensation_ev", "Compensation", static value => value.PostProcess.ExposureCompensationEv, WriteExposureCompensation, SceneDiagnosticCodes.EnvironmentExposureCompensationInvalid, "Exposure compensation must be finite.", "environment.post_process.exposure_compensation_ev"));
        descriptors.Add(FloatDescriptor("/post_process/exposure_key", "Key", static value => value.PostProcess.ExposureKey, static (value, next) => value with { PostProcess = value.PostProcess with { ExposureKey = next } }, SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Post-process values must be finite.", "environment.post_process.exposure_key"));
        descriptors.Add(EnumDescriptor("/post_process/tone_mapper", "Mapper", static value => value.PostProcess.ToneMapper, WriteToneMapping, SceneDiagnosticCodes.EnvironmentToneMappingInvalid, "Tone mapping mode is not valid.", "environment.post_process.tone_mapper"));
        descriptors.Add(EnumDescriptor("/post_process/auto_exposure_metering_mode", "Metering", static value => value.PostProcess.AutoExposureMeteringMode, static (value, next) => value with { PostProcess = value.PostProcess with { AutoExposureMeteringMode = next } }, SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Post-process values must be finite.", "environment.post_process.auto_exposure_metering_mode"));
    }

    private static void AddPostProcessFloatDescriptors(List<PropertyDescriptor> descriptors)
    {
        AddPostFloat(descriptors, "/post_process/auto_exposure_min_ev", "Auto Min EV", static p => p.AutoExposureMinEv, static (p, v) => p with { AutoExposureMinEv = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_max_ev", "Auto Max EV", static p => p.AutoExposureMaxEv, static (p, v) => p with { AutoExposureMaxEv = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_speed_up", "Adapt Up", static p => p.AutoExposureSpeedUp, static (p, v) => p with { AutoExposureSpeedUp = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_speed_down", "Adapt Down", static p => p.AutoExposureSpeedDown, static (p, v) => p with { AutoExposureSpeedDown = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_low_percentile", "Low Percentile", static p => p.AutoExposureLowPercentile, static (p, v) => p with { AutoExposureLowPercentile = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_high_percentile", "High Percentile", static p => p.AutoExposureHighPercentile, static (p, v) => p with { AutoExposureHighPercentile = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_min_log_luminance", "Min Log Luminance", static p => p.AutoExposureMinLogLuminance, static (p, v) => p with { AutoExposureMinLogLuminance = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_log_luminance_range", "Log Luminance Range", static p => p.AutoExposureLogLuminanceRange, static (p, v) => p with { AutoExposureLogLuminanceRange = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_target_luminance", "Target Luminance", static p => p.AutoExposureTargetLuminance, static (p, v) => p with { AutoExposureTargetLuminance = v });
        AddPostFloat(descriptors, "/post_process/auto_exposure_spot_meter_radius", "Spot Radius", static p => p.AutoExposureSpotMeterRadius, static (p, v) => p with { AutoExposureSpotMeterRadius = v });
        AddPostFloat(descriptors, "/post_process/bloom_intensity", "Intensity", static p => p.BloomIntensity, static (p, v) => p with { BloomIntensity = v });
        AddPostFloat(descriptors, "/post_process/bloom_threshold", "Threshold", static p => p.BloomThreshold, static (p, v) => p with { BloomThreshold = v });
        AddPostFloat(descriptors, "/post_process/saturation", "Saturation", static p => p.Saturation, static (p, v) => p with { Saturation = v });
        AddPostFloat(descriptors, "/post_process/contrast", "Contrast", static p => p.Contrast, static (p, v) => p with { Contrast = v });
        AddPostFloat(descriptors, "/post_process/vignette_intensity", "Vignette", static p => p.VignetteIntensity, static (p, v) => p with { VignetteIntensity = v });
        AddPostFloat(descriptors, "/post_process/display_gamma", "Display Gamma", static p => p.DisplayGamma, static (p, v) => p with { DisplayGamma = v });
    }

    private static void AddSkyAtmosphereDescriptors(List<PropertyDescriptor> descriptors)
    {
        AddSkyFloat(descriptors, "/sky_atmosphere/planet_radius_meters", "Planet Radius", static s => s.PlanetRadiusMeters, static (s, v) => s with { PlanetRadiusMeters = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/atmosphere_height_meters", "Atmosphere Height", static s => s.AtmosphereHeightMeters, static (s, v) => s with { AtmosphereHeightMeters = v });
        AddSkyVector(descriptors, "/sky_atmosphere/ground_albedo_rgb", "Ground Albedo", static s => s.GroundAlbedoRgb, static (s, v) => s with { GroundAlbedoRgb = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/rayleigh_scale_height_meters", "Rayleigh Height", static s => s.RayleighScaleHeightMeters, static (s, v) => s with { RayleighScaleHeightMeters = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/mie_scale_height_meters", "Mie Height", static s => s.MieScaleHeightMeters, static (s, v) => s with { MieScaleHeightMeters = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/mie_anisotropy", "Mie Anisotropy", static s => s.MieAnisotropy, static (s, v) => s with { MieAnisotropy = v });
        AddSkyVector(descriptors, "/sky_atmosphere/sky_luminance_factor_rgb", "Sky Luminance", static s => s.SkyLuminanceFactorRgb, static (s, v) => s with { SkyLuminanceFactorRgb = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/aerial_perspective_distance_scale", "Aerial Distance", static s => s.AerialPerspectiveDistanceScale, static (s, v) => s with { AerialPerspectiveDistanceScale = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/aerial_scattering_strength", "Aerial Strength", static s => s.AerialScatteringStrength, static (s, v) => s with { AerialScatteringStrength = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/aerial_perspective_start_depth_meters", "Aerial Start", static s => s.AerialPerspectiveStartDepthMeters, static (s, v) => s with { AerialPerspectiveStartDepthMeters = v });
        AddSkyFloat(descriptors, "/sky_atmosphere/height_fog_contribution", "Height Fog", static s => s.HeightFogContribution, static (s, v) => s with { HeightFogContribution = v });
        descriptors.Add(BoolDescriptor("/sky_atmosphere/sun_disk_enabled", "Sun Disk", static value => value.SkyAtmosphere.SunDiskEnabled, static (value, next) => value with { SkyAtmosphere = value.SkyAtmosphere with { SunDiskEnabled = next } }, "environment.sky_atmosphere.sun_disk_enabled"));
    }

    private static SceneEnvironmentData GetValue(object target)
        => ((SceneDocumentCommandService.SceneEnvironmentPropertyTarget)target).Value;

    private static void SetValue(object target, SceneEnvironmentData value)
        => ((SceneDocumentCommandService.SceneEnvironmentPropertyTarget)target).Value = value;

    private static SceneEnvironmentData WriteExposureMode(SceneEnvironmentData value, ExposureMode next)
        => value with
        {
            ExposureMode = next,
            PostProcess = value.PostProcess with { ExposureMode = next },
        };

    private static SceneEnvironmentData WriteManualExposure(SceneEnvironmentData value, float next)
        => value with
        {
            ManualExposureEv = next,
            PostProcess = value.PostProcess with { ManualExposureEv = next },
        };

    private static SceneEnvironmentData WriteExposureCompensation(SceneEnvironmentData value, float next)
        => value with
        {
            ExposureCompensation = next,
            PostProcess = value.PostProcess with { ExposureCompensationEv = next },
        };

    private static SceneEnvironmentData WriteToneMapping(SceneEnvironmentData value, ToneMappingMode next)
        => value with
        {
            ToneMapping = next,
            PostProcess = value.PostProcess with { ToneMapper = next },
        };

    private static PropertyDescriptor<bool> BoolDescriptor(
        string pointer,
        string label,
        Func<SceneEnvironmentData, bool> read,
        Func<SceneEnvironmentData, bool, SceneEnvironmentData> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<bool>(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            reader: target => read(GetValue(target)),
            writer: (target, value) => SetValue(target, write(GetValue(target), value)),
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation(pointer, new EditorAnnotation { Group = "Environment", Label = label, Renderer = "toggle" }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<Guid?> NullableGuidDescriptor(
        string pointer,
        string label,
        Func<SceneEnvironmentData, Guid?> read,
        Func<SceneEnvironmentData, Guid?, SceneEnvironmentData> write,
        string engineCommandKey)
        => new(
            id: new PropertyId<Guid?>(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            reader: target => read(GetValue(target)),
            writer: (target, value) => SetValue(target, write(GetValue(target), value)),
            validator: static _ => ValidationResult.Ok,
            annotation: Annotation(pointer, new EditorAnnotation { Group = "Environment", Label = label, Renderer = "combo" }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<float> FloatDescriptor(
        string pointer,
        string label,
        Func<SceneEnvironmentData, float> read,
        Func<SceneEnvironmentData, float, SceneEnvironmentData> write,
        string diagnosticCode,
        string diagnosticMessage,
        string engineCommandKey)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            reader: target => read(GetValue(target)),
            writer: (target, value) => SetValue(target, write(GetValue(target), value)),
            validator: value => float.IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(diagnosticCode, diagnosticMessage),
            annotation: Annotation(pointer, new EditorAnnotation { Group = "Environment", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<T> EnumDescriptor<T>(
        string pointer,
        string label,
        Func<SceneEnvironmentData, T> read,
        Func<SceneEnvironmentData, T, SceneEnvironmentData> write,
        string diagnosticCode,
        string diagnosticMessage,
        string engineCommandKey)
        where T : struct, Enum
        => new(
            id: new PropertyId<T>(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            reader: target => read(GetValue(target)),
            writer: (target, value) => SetValue(target, write(GetValue(target), value)),
            validator: value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(diagnosticCode, diagnosticMessage),
            annotation: Annotation(pointer, new EditorAnnotation { Group = "Environment", Label = label, Renderer = "combo" }),
            engineCommandKey: engineCommandKey);

    private static PropertyDescriptor<Vector3> VectorDescriptor(
        string pointer,
        string label,
        Func<SceneEnvironmentData, Vector3> read,
        Func<SceneEnvironmentData, Vector3, SceneEnvironmentData> write,
        string diagnosticCode,
        string diagnosticMessage,
        string engineCommandKey)
        => new(
            id: new PropertyId<Vector3>(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            reader: target => read(GetValue(target)),
            writer: (target, value) => SetValue(target, write(GetValue(target), value)),
            validator: value => IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(diagnosticCode, diagnosticMessage),
            annotation: Annotation(pointer, new EditorAnnotation { Group = "Environment", Label = label, Renderer = "vector3-box", Step = 0.01 }),
            engineCommandKey: engineCommandKey);

    private static EditorAnnotation Annotation(string pointer, EditorAnnotation fallback)
        => SceneEditorSchemaAnnotations.Get(ToSceneSchemaPointer(pointer), fallback);

    private static string ToSceneSchemaPointer(string pointer)
        => pointer switch
        {
            "/atmosphere_enabled" => "#/definitions/editor_scene_environment/atmosphere_enabled",
            "/sun_node_id" => "#/definitions/editor_scene_environment/sun_node_id",
            "/background_color" => "#/definitions/editor_scene_environment/background_color",
            "/post_process/exposure_mode" => "#/definitions/editor_scene_environment/post_process/exposure_mode",
            "/post_process/exposure_enabled" => "#/definitions/editor_scene_environment/post_process/exposure_enabled",
            "/post_process/manual_exposure_ev" => "#/definitions/editor_scene_environment/post_process/manual_exposure_ev",
            "/post_process/exposure_compensation_ev" => "#/definitions/editor_scene_environment/post_process/exposure_compensation_ev",
            "/post_process/exposure_key" => "#/definitions/editor_scene_environment/post_process/exposure_key",
            "/post_process/tone_mapper" => "#/definitions/editor_scene_environment/post_process/tone_mapper",
            "/post_process/auto_exposure_metering_mode" => "#/definitions/editor_scene_environment/post_process/auto_exposure_metering_mode",
            "/post_process/auto_exposure_min_ev" => "#/definitions/editor_scene_environment/post_process/auto_exposure_min_ev",
            "/post_process/auto_exposure_max_ev" => "#/definitions/editor_scene_environment/post_process/auto_exposure_max_ev",
            "/post_process/auto_exposure_speed_up" => "#/definitions/editor_scene_environment/post_process/auto_exposure_speed_up",
            "/post_process/auto_exposure_speed_down" => "#/definitions/editor_scene_environment/post_process/auto_exposure_speed_down",
            "/post_process/auto_exposure_low_percentile" => "#/definitions/editor_scene_environment/post_process/auto_exposure_low_percentile",
            "/post_process/auto_exposure_high_percentile" => "#/definitions/editor_scene_environment/post_process/auto_exposure_high_percentile",
            "/post_process/auto_exposure_min_log_luminance" => "#/definitions/editor_scene_environment/post_process/auto_exposure_min_log_luminance",
            "/post_process/auto_exposure_log_luminance_range" => "#/definitions/editor_scene_environment/post_process/auto_exposure_log_luminance_range",
            "/post_process/auto_exposure_target_luminance" => "#/definitions/editor_scene_environment/post_process/auto_exposure_target_luminance",
            "/post_process/auto_exposure_spot_meter_radius" => "#/definitions/editor_scene_environment/post_process/auto_exposure_spot_meter_radius",
            "/post_process/bloom_intensity" => "#/definitions/editor_scene_environment/post_process/bloom_intensity",
            "/post_process/bloom_threshold" => "#/definitions/editor_scene_environment/post_process/bloom_threshold",
            "/post_process/saturation" => "#/definitions/editor_scene_environment/post_process/saturation",
            "/post_process/contrast" => "#/definitions/editor_scene_environment/post_process/contrast",
            "/post_process/vignette_intensity" => "#/definitions/editor_scene_environment/post_process/vignette_intensity",
            "/post_process/display_gamma" => "#/definitions/editor_scene_environment/post_process/display_gamma",
            "/sky_atmosphere/planet_radius_meters" => "#/definitions/sky_atmosphere_environment/planet_radius_m",
            "/sky_atmosphere/atmosphere_height_meters" => "#/definitions/sky_atmosphere_environment/atmosphere_height_m",
            "/sky_atmosphere/ground_albedo_rgb" => "#/definitions/sky_atmosphere_environment/ground_albedo_rgb",
            "/sky_atmosphere/rayleigh_scale_height_meters" => "#/definitions/sky_atmosphere_environment/rayleigh_scale_height_m",
            "/sky_atmosphere/mie_scale_height_meters" => "#/definitions/sky_atmosphere_environment/mie_scale_height_m",
            "/sky_atmosphere/mie_anisotropy" => "#/definitions/sky_atmosphere_environment/mie_anisotropy",
            "/sky_atmosphere/sky_luminance_factor_rgb" => "#/definitions/sky_atmosphere_environment/sky_luminance_factor_rgb",
            "/sky_atmosphere/aerial_perspective_distance_scale" => "#/definitions/sky_atmosphere_environment/aerial_perspective_distance_scale",
            "/sky_atmosphere/aerial_scattering_strength" => "#/definitions/sky_atmosphere_environment/aerial_scattering_strength",
            "/sky_atmosphere/aerial_perspective_start_depth_meters" => "#/definitions/sky_atmosphere_environment/aerial_perspective_start_depth_m",
            "/sky_atmosphere/height_fog_contribution" => "#/definitions/sky_atmosphere_environment/height_fog_contribution",
            "/sky_atmosphere/sun_disk_enabled" => "#/definitions/sky_atmosphere_environment/sun_disk_enabled",
            _ => throw new ArgumentOutOfRangeException(nameof(pointer), pointer, "Unknown scene environment property pointer."),
        };

    private static void AddPostFloat(
        List<PropertyDescriptor> descriptors,
        string pointer,
        string label,
        Func<PostProcessEnvironmentData, float> read,
        Func<PostProcessEnvironmentData, float, PostProcessEnvironmentData> write)
        => descriptors.Add(FloatDescriptor(
            pointer,
            label,
            value => read(value.PostProcess),
            (value, next) => value with { PostProcess = write(value.PostProcess, next) },
            SceneDiagnosticCodes.EnvironmentManualExposureInvalid,
            "Post-process values must be finite.",
            $"environment{pointer.Replace('/', '.')}"));

    private static void AddSkyFloat(
        List<PropertyDescriptor> descriptors,
        string pointer,
        string label,
        Func<SkyAtmosphereEnvironmentData, float> read,
        Func<SkyAtmosphereEnvironmentData, float, SkyAtmosphereEnvironmentData> write)
        => descriptors.Add(FloatDescriptor(
            pointer,
            label,
            value => read(value.SkyAtmosphere),
            (value, next) => value with { SkyAtmosphere = write(value.SkyAtmosphere, next) },
            SceneDiagnosticCodes.EnvironmentSkyAtmosphereInvalid,
            "Sky atmosphere values must be finite.",
            $"environment{pointer.Replace('/', '.')}"));

    private static void AddSkyVector(
        List<PropertyDescriptor> descriptors,
        string pointer,
        string label,
        Func<SkyAtmosphereEnvironmentData, Vector3> read,
        Func<SkyAtmosphereEnvironmentData, Vector3, SkyAtmosphereEnvironmentData> write)
        => descriptors.Add(VectorDescriptor(
            pointer,
            label,
            value => read(value.SkyAtmosphere),
            (value, next) => value with { SkyAtmosphere = write(value.SkyAtmosphere, next) },
            SceneDiagnosticCodes.EnvironmentSkyAtmosphereInvalid,
            "Sky atmosphere values must be finite.",
            $"environment{pointer.Replace('/', '.')}"));

    private static bool IsFinite(Vector3 value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private PropertyDescriptor<T> Get<T>(string pointer)
        => (PropertyDescriptor<T>)this.ById[new PropertyId(SceneDocumentCommandService.SceneEnvironmentKind, pointer)];
}
