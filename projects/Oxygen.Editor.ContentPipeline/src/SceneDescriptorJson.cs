// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

// This file intentionally groups the private DTOs that mirror the native JSON schema.
#pragma warning disable SA1402, SA1600

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

internal static class SceneDescriptorJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        WriteIndented = true,
    };
}

internal sealed record NativeSceneDescriptor(
    [property: JsonPropertyName("$schema")] string Schema,
    [property: JsonPropertyName("version")] int Version,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("nodes")] IReadOnlyList<NativeSceneNode> Nodes,
    [property: JsonPropertyName("renderables")] IReadOnlyList<NativeRenderable>? Renderables,
    [property: JsonPropertyName("cameras")] NativeCameras? Cameras,
    [property: JsonPropertyName("lights")] NativeLights? Lights,
    [property: JsonPropertyName("environment")] NativeEnvironment? Environment,
    [property: JsonPropertyName("references")] NativeReferences? References);

internal sealed record NativeSceneNode(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("parent")] int? Parent,
    [property: JsonPropertyName("flags")] NativeNodeFlags Flags,
    [property: JsonPropertyName("transform")] NativeNodeTransform Transform);

internal sealed record NativeNodeFlags(
    [property: JsonPropertyName("visible")] string Visible,
    [property: JsonPropertyName("static")] bool Static,
    [property: JsonPropertyName("casts_shadows")] string CastsShadows,
    [property: JsonPropertyName("receives_shadows")] string ReceivesShadows,
    [property: JsonPropertyName("ray_cast_selectable")] bool RayCastSelectable,
    [property: JsonPropertyName("ignore_parent_transform")] bool IgnoreParentTransform);

internal sealed record NativeNodeTransform(
    [property: JsonPropertyName("translation")] float[] Translation,
    [property: JsonPropertyName("rotation")] float[] Rotation,
    [property: JsonPropertyName("scale")] float[] Scale);

internal sealed record NativeRenderable(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("geometry_ref")] string GeometryRef,
    [property: JsonPropertyName("material_overrides")] IReadOnlyList<NativeMaterialSlotOverride> MaterialOverrides,
    [property: JsonPropertyName("visible")] bool Visible);

internal sealed record NativeMaterialSlotOverride(
    [property: JsonPropertyName("slot_id")] Guid SlotId,
    [property: JsonPropertyName("material_ref")] string MaterialRef,
    [property: JsonPropertyName("layout_revision")] string LayoutRevision);

internal sealed record NativeCameras(
    [property: JsonPropertyName("perspective")] IReadOnlyList<NativePerspectiveCamera>? Perspective,
    [property: JsonPropertyName("orthographic")] IReadOnlyList<NativeOrthographicCamera>? Orthographic);

internal sealed record NativePerspectiveCamera(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("fov_y")] float FieldOfViewY,
    [property: JsonPropertyName("aspect_ratio")] float AspectRatio,
    [property: JsonPropertyName("aspect_mode")] string AspectMode,
    [property: JsonPropertyName("near_plane")] float NearPlane,
    [property: JsonPropertyName("far_plane")] float FarPlane,
    [property: JsonPropertyName("aperture_f")] float ApertureF,
    [property: JsonPropertyName("shutter_rate")] float ShutterRate,
    [property: JsonPropertyName("iso")] float Iso);

internal sealed record NativeOrthographicCamera(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("aspect_mode")] string AspectMode,
    [property: JsonPropertyName("orthographic_size")] float OrthographicSize,
    [property: JsonPropertyName("aspect_ratio")] float AspectRatio,
    [property: JsonPropertyName("near_plane")] float NearPlane,
    [property: JsonPropertyName("far_plane")] float FarPlane,
    [property: JsonPropertyName("aperture_f")] float ApertureF,
    [property: JsonPropertyName("shutter_rate")] float ShutterRate,
    [property: JsonPropertyName("iso")] float Iso);

internal sealed record NativeLights(
    [property: JsonPropertyName("directional")] IReadOnlyList<NativeDirectionalLight>? Directional,
    [property: JsonPropertyName("point")] IReadOnlyList<NativePointLight>? Point,
    [property: JsonPropertyName("spot")] IReadOnlyList<NativeSpotLight>? Spot);

internal sealed record NativeLightCommon(
    [property: JsonPropertyName("affects_world")] bool AffectsWorld,
    [property: JsonPropertyName("color_rgb")] float[] ColorRgb,
    [property: JsonPropertyName("casts_shadows")] bool CastsShadows,
    [property: JsonPropertyName("exposure_compensation_ev")] float ExposureCompensation,
    [property: JsonPropertyName("shadow")] NativeLightShadow Shadow);

internal sealed record NativeLightShadow(
    [property: JsonPropertyName("bias")] float Bias,
    [property: JsonPropertyName("normal_bias")] float NormalBias,
    [property: JsonPropertyName("contact_shadows")] bool ContactShadows,
    [property: JsonPropertyName("resolution_hint")] int ResolutionHint);

internal sealed record NativeDirectionalLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("intensity_lux")] float IntensityLux,
    [property: JsonPropertyName("angular_size_radians")] float AngularSizeRadians,
    [property: JsonPropertyName("atmosphere_light_slot")] int AtmosphereLightSlot,
    [property: JsonPropertyName("use_per_pixel_atmosphere_transmittance")] bool UsePerPixelAtmosphereTransmittance,
    [property: JsonPropertyName("atmosphere_disk_luminance_scale_rgb")] float[] AtmosphereDiskLuminanceScaleRgb,
    [property: JsonPropertyName("cascade_count")] int CascadeCount,
    [property: JsonPropertyName("split_mode")] int SplitMode,
    [property: JsonPropertyName("max_shadow_distance")] float MaxShadowDistance,
    [property: JsonPropertyName("cascade_distances")] float[] CascadeDistances,
    [property: JsonPropertyName("distribution_exponent")] float DistributionExponent,
    [property: JsonPropertyName("transition_fraction")] float TransitionFraction,
    [property: JsonPropertyName("distance_fadeout_fraction")] float DistanceFadeoutFraction);

internal sealed record NativePointLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("luminous_flux_lm")] float LuminousFluxLumens,
    [property: JsonPropertyName("range")] float Range,
    [property: JsonPropertyName("source_radius")] float SourceRadius);

internal sealed record NativeSpotLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("luminous_flux_lm")] float LuminousFluxLumens,
    [property: JsonPropertyName("range")] float Range,
    [property: JsonPropertyName("source_radius")] float SourceRadius,
    [property: JsonPropertyName("inner_cone_angle_radians")] float InnerConeAngleRadians,
    [property: JsonPropertyName("outer_cone_angle_radians")] float OuterConeAngleRadians);

internal sealed record NativeEnvironment(
    [property: JsonPropertyName("sky_atmosphere")] NativeSkyAtmosphereEnvironment SkyAtmosphere,
    [property: JsonPropertyName("sky_light")] NativeSkyLightEnvironment SkyLight,
    [property: JsonPropertyName("fog")] NativeFogEnvironment Fog,
    [property: JsonPropertyName("post_process_volume")] NativePostProcessEnvironment PostProcess,
    [property: JsonPropertyName("background")] NativeBackgroundEnvironment Background);

internal sealed record NativeFogEnvironment(
    [property: JsonPropertyName("enabled")] bool Enabled,
    [property: JsonPropertyName("enable_height_fog")] bool HeightFogEnabled,
    [property: JsonPropertyName("extinction_sigma_t_per_m")] float Density,
    [property: JsonPropertyName("height_falloff_per_m")] float HeightFalloff,
    [property: JsonPropertyName("height_offset_m")] float HeightOffsetMeters,
    [property: JsonPropertyName("max_opacity")] float MaxOpacity,
    [property: JsonPropertyName("fog_inscattering_luminance")] float[] InscatteringLuminanceRgb,
    [property: JsonPropertyName("sky_atmosphere_ambient_contribution_color_scale")] float[] SkyAmbientScaleRgb,
    [property: JsonPropertyName("second_fog_density")] float SecondDensity,
    [property: JsonPropertyName("second_fog_height_falloff")] float SecondHeightFalloff,
    [property: JsonPropertyName("second_fog_height_offset")] float SecondHeightOffsetMeters,
    [property: JsonPropertyName("start_distance_m")] float StartDistanceMeters,
    [property: JsonPropertyName("end_distance_m")] float EndDistanceMeters,
    [property: JsonPropertyName("fog_cutoff_distance_m")] float CutoffDistanceMeters,
    [property: JsonPropertyName("directional_inscattering_luminance")] float[] DirectionalInscatteringLuminanceRgb,
    [property: JsonPropertyName("directional_inscattering_exponent")] float DirectionalInscatteringExponent,
    [property: JsonPropertyName("directional_inscattering_start_distance")] float DirectionalInscatteringStartDistanceMeters,
    [property: JsonPropertyName("enable_volumetric_fog")] bool VolumetricFogEnabled,
    [property: JsonPropertyName("volumetric_fog_scattering_distribution")] float VolumetricScatteringDistribution,
    [property: JsonPropertyName("volumetric_fog_albedo")] float[] VolumetricAlbedoRgb,
    [property: JsonPropertyName("volumetric_fog_emissive")] float[] VolumetricEmissiveRgb,
    [property: JsonPropertyName("volumetric_fog_extinction_scale")] float VolumetricExtinctionScale,
    [property: JsonPropertyName("volumetric_fog_distance")] float VolumetricDistanceMeters,
    [property: JsonPropertyName("volumetric_fog_start_distance")] float VolumetricStartDistanceMeters,
    [property: JsonPropertyName("volumetric_fog_near_fade_in_distance")] float VolumetricNearFadeInDistanceMeters,
    [property: JsonPropertyName("volumetric_fog_static_lighting_scattering_intensity")] float VolumetricStaticLightingScatteringIntensity,
    [property: JsonPropertyName("override_light_colors_with_fog_inscattering_colors")] bool OverrideLightColorsWithFogInscattering,
    [property: JsonPropertyName("render_in_main_pass")] bool RenderInMainPass,
    [property: JsonPropertyName("holdout")] bool Holdout,
    [property: JsonPropertyName("visible_in_reflection_captures")] bool VisibleInReflectionCaptures,
    [property: JsonPropertyName("visible_in_real_time_sky_captures")] bool VisibleInRealTimeSkyCaptures,
    [property: JsonPropertyName("model")] int Model,
    [property: JsonPropertyName("single_scattering_albedo_rgb")] float[] SingleScatteringAlbedoRgb,
    [property: JsonPropertyName("anisotropy_g")] float AnisotropyG,
    [property: JsonPropertyName("inscattering_color_cubemap_angle")] float InscatteringColorCubemapAngle,
    [property: JsonPropertyName("inscattering_texture_tint")] float[] InscatteringTextureTint,
    [property: JsonPropertyName("fully_directional_inscattering_color_distance")] float FullyDirectionalInscatteringColorDistance,
    [property: JsonPropertyName("non_directional_inscattering_color_distance")] float NonDirectionalInscatteringColorDistance);

internal sealed record NativeSkyLightEnvironment(
    [property: JsonPropertyName("enabled")] bool Enabled,
    [property: JsonPropertyName("source")] int Source,
    [property: JsonPropertyName("intensity")] float Intensity,
    [property: JsonPropertyName("tint_rgb")] float[] TintRgb,
    [property: JsonPropertyName("diffuse_intensity")] float DiffuseIntensity,
    [property: JsonPropertyName("specular_intensity")] float SpecularIntensity,
    [property: JsonPropertyName("source_cubemap_angle_radians")] float SourceCubemapAngleRadians,
    [property: JsonPropertyName("lower_hemisphere_color")] float[] LowerHemisphereColor,
    [property: JsonPropertyName("lower_hemisphere_is_solid_color")] bool LowerHemisphereIsSolidColor,
    [property: JsonPropertyName("lower_hemisphere_blend_alpha")] float LowerHemisphereBlendAlpha,
    [property: JsonPropertyName("volumetric_scattering_intensity")] float VolumetricScatteringIntensity,
    [property: JsonPropertyName("affect_reflections")] bool AffectReflections);

internal sealed record NativeBackgroundEnvironment(
    [property: JsonPropertyName("enabled")] bool Enabled,
    [property: JsonPropertyName("color_rgb")] float[] ColorRgb);

internal sealed record NativePostProcessEnvironment(
    [property: JsonPropertyName("enabled")] bool Enabled,
    [property: JsonPropertyName("tone_mapper")] int ToneMapper,
    [property: JsonPropertyName("exposure_mode")] int ExposureMode,
    [property: JsonPropertyName("exposure_enabled")] bool ExposureEnabled,
    [property: JsonPropertyName("exposure_compensation_ev")] float ExposureCompensationEv,
    [property: JsonPropertyName("exposure_key")] float ExposureKey,
    [property: JsonPropertyName("manual_exposure_ev")] float ManualExposureEv,
    [property: JsonPropertyName("auto_exposure_min_ev")] float AutoExposureMinEv,
    [property: JsonPropertyName("auto_exposure_max_ev")] float AutoExposureMaxEv,
    [property: JsonPropertyName("auto_exposure_speed_up")] float AutoExposureSpeedUp,
    [property: JsonPropertyName("auto_exposure_speed_down")] float AutoExposureSpeedDown,
    [property: JsonPropertyName("auto_exposure_metering_mode")] int AutoExposureMeteringMode,
    [property: JsonPropertyName("auto_exposure_low_percentile")] float AutoExposureLowPercentile,
    [property: JsonPropertyName("auto_exposure_high_percentile")] float AutoExposureHighPercentile,
    [property: JsonPropertyName("auto_exposure_min_log_luminance")] float AutoExposureMinLogLuminance,
    [property: JsonPropertyName("auto_exposure_log_luminance_range")] float AutoExposureLogLuminanceRange,
    [property: JsonPropertyName("auto_exposure_target_luminance")] float AutoExposureTargetLuminance,
    [property: JsonPropertyName("auto_exposure_spot_meter_radius")] float AutoExposureSpotMeterRadius,
    [property: JsonPropertyName("auto_exposure_black_influence")] float AutoExposureBlackInfluence,
    [property: JsonPropertyName("auto_exposure_transition_distance_ev")] float AutoExposureTransitionDistanceEv,
    [property: JsonPropertyName("auto_exposure_metering_mask")] string? AutoExposureMeteringMask,
    [property: JsonPropertyName("auto_exposure_compensation_curve")] IReadOnlyList<NativeExposureCompensationKey> AutoExposureCompensationCurve,
    [property: JsonPropertyName("bloom_intensity")] float BloomIntensity,
    [property: JsonPropertyName("bloom_threshold")] float BloomThreshold,
    [property: JsonPropertyName("saturation")] float Saturation,
    [property: JsonPropertyName("contrast")] float Contrast,
    [property: JsonPropertyName("vignette_intensity")] float VignetteIntensity,
    [property: JsonPropertyName("display_gamma")] float DisplayGamma);

internal sealed record NativeExposureCompensationKey(
    [property: JsonPropertyName("metered_ev")] float MeteredEv,
    [property: JsonPropertyName("compensation_ev")] float CompensationEv);

internal sealed record NativeSkyAtmosphereEnvironment(
    [property: JsonPropertyName("enabled")] bool Enabled,
    [property: JsonPropertyName("planet_radius_m")] float PlanetRadiusMeters,
    [property: JsonPropertyName("atmosphere_height_m")] float AtmosphereHeightMeters,
    [property: JsonPropertyName("ground_albedo_rgb")] float[] GroundAlbedoRgb,
    [property: JsonPropertyName("rayleigh_scattering_rgb")] float[] RayleighScatteringRgb,
    [property: JsonPropertyName("rayleigh_scale_height_m")] float RayleighScaleHeightMeters,
    [property: JsonPropertyName("mie_scattering_rgb")] float[] MieScatteringRgb,
    [property: JsonPropertyName("mie_absorption_rgb")] float[] MieAbsorptionRgb,
    [property: JsonPropertyName("mie_scale_height_m")] float MieScaleHeightMeters,
    [property: JsonPropertyName("mie_anisotropy")] float MieAnisotropy,
    [property: JsonPropertyName("ozone_absorption_rgb")] float[] OzoneAbsorptionRgb,
    [property: JsonPropertyName("ozone_density_profile")] float[] OzoneDensityProfile,
    [property: JsonPropertyName("multi_scattering_factor")] float MultiScatteringFactor,
    [property: JsonPropertyName("sky_luminance_factor_rgb")] float[] SkyLuminanceFactorRgb,
    [property: JsonPropertyName("sky_and_aerial_perspective_luminance_factor_rgb")] float[] SkyAndAerialPerspectiveLuminanceFactorRgb,
    [property: JsonPropertyName("aerial_perspective_distance_scale")] float AerialPerspectiveDistanceScale,
    [property: JsonPropertyName("aerial_scattering_strength")] float AerialScatteringStrength,
    [property: JsonPropertyName("aerial_perspective_start_depth_m")] float AerialPerspectiveStartDepthMeters,
    [property: JsonPropertyName("height_fog_contribution")] float HeightFogContribution,
    [property: JsonPropertyName("trace_sample_count_scale")] float TraceSampleCountScale,
    [property: JsonPropertyName("transmittance_min_light_elevation_deg")] float TransmittanceMinLightElevationDegrees,
    [property: JsonPropertyName("sun_disk_enabled")] bool SunDiskEnabled,
    [property: JsonPropertyName("holdout")] bool Holdout,
    [property: JsonPropertyName("render_in_main_pass")] bool RenderInMainPass);

internal sealed record NativeReferences(
    [property: JsonPropertyName("materials")] IReadOnlyList<string>? Materials,
    [property: JsonPropertyName("scripts")] IReadOnlyList<string>? Scripts,
    [property: JsonPropertyName("input_actions")] IReadOnlyList<string>? InputActions,
    [property: JsonPropertyName("input_mapping_contexts")] IReadOnlyList<string>? InputMappingContexts,
    [property: JsonPropertyName("physics_sidecars")] IReadOnlyList<string>? PhysicsSidecars,
    [property: JsonPropertyName("extra_assets")] IReadOnlyList<string>? ExtraAssets);
