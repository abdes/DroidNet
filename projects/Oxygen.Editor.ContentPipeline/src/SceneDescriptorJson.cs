// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

// This file intentionally groups the private DTOs that mirror the native JSON schema.
#pragma warning disable SA1402
#pragma warning disable SA1600

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
    [property: JsonPropertyName("visible")] bool Visible,
    [property: JsonPropertyName("static")] bool Static,
    [property: JsonPropertyName("casts_shadows")] bool CastsShadows,
    [property: JsonPropertyName("receives_shadows")] bool ReceivesShadows,
    [property: JsonPropertyName("ray_cast_selectable")] bool RayCastSelectable,
    [property: JsonPropertyName("ignore_parent_transform")] bool IgnoreParentTransform);

internal sealed record NativeNodeTransform(
    [property: JsonPropertyName("translation")] float[] Translation,
    [property: JsonPropertyName("rotation")] float[] Rotation,
    [property: JsonPropertyName("scale")] float[] Scale);

internal sealed record NativeRenderable(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("geometry_ref")] string GeometryRef,
    [property: JsonPropertyName("material_ref")] string? MaterialRef,
    [property: JsonPropertyName("visible")] bool Visible);

internal sealed record NativeCameras(
    [property: JsonPropertyName("perspective")] IReadOnlyList<NativePerspectiveCamera>? Perspective);

internal sealed record NativePerspectiveCamera(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("fov_y")] float FieldOfViewY,
    [property: JsonPropertyName("aspect_ratio")] float AspectRatio,
    [property: JsonPropertyName("near_plane")] float NearPlane,
    [property: JsonPropertyName("far_plane")] float FarPlane);

internal sealed record NativeLights(
    [property: JsonPropertyName("directional")] IReadOnlyList<NativeDirectionalLight>? Directional,
    [property: JsonPropertyName("point")] IReadOnlyList<NativePointLight>? Point,
    [property: JsonPropertyName("spot")] IReadOnlyList<NativeSpotLight>? Spot);

internal sealed record NativeLightCommon(
    [property: JsonPropertyName("affects_world")] bool AffectsWorld,
    [property: JsonPropertyName("color_rgb")] float[] ColorRgb,
    [property: JsonPropertyName("casts_shadows")] bool CastsShadows,
    [property: JsonPropertyName("exposure_compensation_ev")] float ExposureCompensation);

internal sealed record NativeDirectionalLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("intensity_lux")] float IntensityLux,
    [property: JsonPropertyName("angular_size_radians")] float AngularSizeRadians,
    [property: JsonPropertyName("environment_contribution")] bool EnvironmentContribution,
    [property: JsonPropertyName("is_sun_light")] bool IsSunLight);

internal sealed record NativePointLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("luminous_flux_lm")] float LuminousFluxLumens,
    [property: JsonPropertyName("range")] float Range,
    [property: JsonPropertyName("source_radius")] float SourceRadius,
    [property: JsonPropertyName("decay_exponent")] float DecayExponent);

internal sealed record NativeSpotLight(
    [property: JsonPropertyName("node")] int Node,
    [property: JsonPropertyName("common")] NativeLightCommon Common,
    [property: JsonPropertyName("luminous_flux_lm")] float LuminousFluxLumens,
    [property: JsonPropertyName("range")] float Range,
    [property: JsonPropertyName("source_radius")] float SourceRadius,
    [property: JsonPropertyName("decay_exponent")] float DecayExponent,
    [property: JsonPropertyName("inner_cone_angle_radians")] float InnerConeAngleRadians,
    [property: JsonPropertyName("outer_cone_angle_radians")] float OuterConeAngleRadians);

internal sealed record NativeEnvironment(
    [property: JsonPropertyName("sky_atmosphere")] NativeSkyAtmosphereEnvironment SkyAtmosphere);

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
    [property: JsonPropertyName("extra_assets")] IReadOnlyList<string>? ExtraAssets);

internal sealed record NativeGeometryDescriptor(
    [property: JsonPropertyName("$schema")] string Schema,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("bounds")] NativeBounds Bounds,
    [property: JsonPropertyName("lods")] IReadOnlyList<NativeGeometryLod> Lods);

internal sealed record NativeBounds(
    [property: JsonPropertyName("min")] float[] Min,
    [property: JsonPropertyName("max")] float[] Max);

internal sealed record NativeGeometryLod(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("mesh_type")] string MeshType,
    [property: JsonPropertyName("bounds")] NativeBounds Bounds,
    [property: JsonPropertyName("procedural")] NativeProceduralDescriptor Procedural,
    [property: JsonPropertyName("submeshes")] IReadOnlyList<NativeSubmeshDescriptor> Submeshes);

internal sealed record NativeProceduralDescriptor(
    [property: JsonPropertyName("generator")] string Generator,
    [property: JsonPropertyName("mesh_name")] string MeshName,
    [property: JsonPropertyName("params")] object? Params);

internal sealed record NativeSubmeshDescriptor(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("material_ref")] string MaterialRef,
    [property: JsonPropertyName("views")] IReadOnlyList<NativeSubmeshView> Views);

internal sealed record NativeSubmeshView([property: JsonPropertyName("view_ref")] string ViewRef);

internal sealed record NativeSphereParams(
    [property: JsonPropertyName("latitude_segments")] int LatitudeSegments,
    [property: JsonPropertyName("longitude_segments")] int LongitudeSegments);

internal sealed record NativePlaneParams(
    [property: JsonPropertyName("x_segments")] int XSegments,
    [property: JsonPropertyName("z_segments")] int ZSegments,
    [property: JsonPropertyName("size")] float Size);
