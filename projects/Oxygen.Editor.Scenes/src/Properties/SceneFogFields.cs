// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>Typed scene-environment property identities for authored fog.</summary>
public static class SceneFogFields
{
    /// <summary>Gets the property for whether the scene renders fog.</summary>
    public static PropertyId<bool> Enabled { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/enabled");

    /// <summary>Gets the property for whether the exponential height fog layers render.</summary>
    public static PropertyId<bool> HeightFogEnabled { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/height_fog_enabled");

    /// <summary>Gets the property for the first layer's extinction coefficient at its height offset, per metre.</summary>
    public static PropertyId<float> Density { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/density");

    /// <summary>Gets the property for how quickly the first layer thins with height, per metre.</summary>
    public static PropertyId<float> HeightFalloff { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/height_falloff");

    /// <summary>Gets the property for the world height at which the first layer has its authored density.</summary>
    public static PropertyId<float> HeightOffsetMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/height_offset_meters");

    /// <summary>Gets the property for the largest fraction of the scene the fog may hide.</summary>
    public static PropertyId<float> MaxOpacity { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/max_opacity");

    /// <summary>Gets the property for the fog's own inscattered luminance.</summary>
    public static PropertyId<Vector3> InscatteringLuminanceRgb { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/inscattering_luminance_rgb");

    /// <summary>Gets the property for the scale applied to ambient light the sky atmosphere contributes to the fog.</summary>
    public static PropertyId<Vector3> SkyAmbientScaleRgb { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/sky_ambient_scale_rgb");

    /// <summary>Gets the property for the second layer's extinction coefficient at its height offset, per metre.</summary>
    public static PropertyId<float> SecondDensity { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/second_density");

    /// <summary>Gets the property for how quickly the second layer thins with height, per metre.</summary>
    public static PropertyId<float> SecondHeightFalloff { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/second_height_falloff");

    /// <summary>Gets the property for the world height at which the second layer has its authored density.</summary>
    public static PropertyId<float> SecondHeightOffsetMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/second_height_offset_meters");

    /// <summary>Gets the property for the camera distance at which fog begins.</summary>
    public static PropertyId<float> StartDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/start_distance_meters");

    /// <summary>Gets the property for the horizontal distance beyond which fog stops accumulating; 0 is unlimited.</summary>
    public static PropertyId<float> EndDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/end_distance_meters");

    /// <summary>Gets the property for the distance beyond which surfaces receive no fog; 0 is unlimited.</summary>
    public static PropertyId<float> CutoffDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/cutoff_distance_meters");

    /// <summary>Gets the property for the inscattered luminance of the lobe toward the primary atmosphere light.</summary>
    public static PropertyId<Vector3> DirectionalInscatteringLuminanceRgb { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/directional_inscattering_luminance_rgb");

    /// <summary>Gets the property for the sharpness of the lobe toward the primary atmosphere light.</summary>
    public static PropertyId<float> DirectionalInscatteringExponent { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/directional_inscattering_exponent");

    /// <summary>Gets the property for the camera distance at which the directional lobe begins.</summary>
    public static PropertyId<float> DirectionalInscatteringStartDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/directional_inscattering_start_distance_meters");

    /// <summary>Gets the property for whether lights scatter through a volumetric fog grid.</summary>
    public static PropertyId<bool> VolumetricFogEnabled { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_fog_enabled");

    /// <summary>Gets the property for the phase anisotropy; positive values scatter forward.</summary>
    public static PropertyId<float> VolumetricScatteringDistribution { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_scattering_distribution");

    /// <summary>Gets the property for the fraction of light the volumetric fog scatters rather than absorbs.</summary>
    public static PropertyId<Vector3> VolumetricAlbedoRgb { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_albedo_rgb");

    /// <summary>Gets the property for the luminance the volumetric fog emits.</summary>
    public static PropertyId<Vector3> VolumetricEmissiveRgb { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_emissive_rgb");

    /// <summary>Gets the property for the scale applied to height fog extinction inside the volumetric grid.</summary>
    public static PropertyId<float> VolumetricExtinctionScale { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_extinction_scale");

    /// <summary>Gets the property for the far end of the volumetric grid; 0 uses the camera far plane.</summary>
    public static PropertyId<float> VolumetricDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_distance_meters");

    /// <summary>Gets the property for the near end of the volumetric grid.</summary>
    public static PropertyId<float> VolumetricStartDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_start_distance_meters");

    /// <summary>Gets the property for the distance over which volumetric fog fades in from its start.</summary>
    public static PropertyId<float> VolumetricNearFadeInDistanceMeters { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_near_fade_in_distance_meters");

    /// <summary>Gets the property for the scale applied to sky lighting scattered by the volumetric fog.</summary>
    public static PropertyId<float> VolumetricStaticLightingScatteringIntensity { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/volumetric_static_lighting_scattering_intensity");

    /// <summary>Gets the property for whether lights scatter with the fog's inscattering colors instead of their own.</summary>
    public static PropertyId<bool> OverrideLightColorsWithFogInscattering { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/override_light_colors_with_fog_inscattering");

    /// <summary>Gets the property for whether the fog renders in the main scene pass.</summary>
    public static PropertyId<bool> RenderInMainPass { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/render_in_main_pass");

    /// <summary>Gets the property for whether the fog cuts out the scene behind it without adding its own color.</summary>
    public static PropertyId<bool> Holdout { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/holdout");

    /// <summary>Gets the property for whether reflection captures include the fog.</summary>
    public static PropertyId<bool> VisibleInReflectionCaptures { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/visible_in_reflection_captures");

    /// <summary>Gets the property for whether sky light captures include the fog.</summary>
    public static PropertyId<bool> VisibleInRealTimeSkyCaptures { get; } = new(SceneProperties.SceneEnvironmentKind, "/fog/visible_in_real_time_sky_captures");
}
