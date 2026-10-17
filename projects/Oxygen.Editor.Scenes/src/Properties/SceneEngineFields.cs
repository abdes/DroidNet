// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scenes.Engine;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>Where each scene property goes in the engine, by component kind and engine command key.</summary>
/// <remarks>
/// Geometry and the scene environment have no entry: the engine takes a geometry asset with its component and the
/// environment as a whole.
/// </remarks>
internal static class SceneEngineFields
{
    private static readonly FrozenDictionary<string, (EngineComponentId Component, ushort[] Fields)> Shared = new Dictionary<string, (EngineComponentId, ushort[])>(StringComparer.Ordinal)
    {
        ["transform.position.x"] = (EngineComponentId.Transform, [(ushort)TransformField.PositionX]),
        ["transform.position.y"] = (EngineComponentId.Transform, [(ushort)TransformField.PositionY]),
        ["transform.position.z"] = (EngineComponentId.Transform, [(ushort)TransformField.PositionZ]),
        ["transform.rotation.x"] = (EngineComponentId.Transform, [(ushort)TransformField.RotationXDegrees]),
        ["transform.rotation.y"] = (EngineComponentId.Transform, [(ushort)TransformField.RotationYDegrees]),
        ["transform.rotation.z"] = (EngineComponentId.Transform, [(ushort)TransformField.RotationZDegrees]),
        ["transform.scale.x"] = (EngineComponentId.Transform, [(ushort)TransformField.ScaleX]),
        ["transform.scale.y"] = (EngineComponentId.Transform, [(ushort)TransformField.ScaleY]),
        ["transform.scale.z"] = (EngineComponentId.Transform, [(ushort)TransformField.ScaleZ]),
        ["perspective_camera.field_of_view_degrees"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.FieldOfViewYRadians]),
        ["perspective_camera.aspect_ratio"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.AspectRatio]),
        ["perspective_camera.near_plane"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.NearPlane]),
        ["perspective_camera.far_plane"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.FarPlane]),
        ["perspective_camera.aperture_f"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.ApertureF]),
        ["perspective_camera.shutter_rate"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.ShutterRate]),
        ["perspective_camera.iso"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.Iso]),
        ["perspective_camera.aspect_mode"] = (EngineComponentId.PerspectiveCamera, [(ushort)PerspectiveCameraField.AspectMode]),
        ["orthographic_camera.orthographic_size"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.OrthographicSize]),
        ["orthographic_camera.aspect_ratio"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.AspectRatio]),
        ["orthographic_camera.near_plane"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.NearPlane]),
        ["orthographic_camera.far_plane"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.FarPlane]),
        ["orthographic_camera.aperture_f"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.ApertureF]),
        ["orthographic_camera.shutter_rate"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.ShutterRate]),
        ["orthographic_camera.iso"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.Iso]),
        ["orthographic_camera.aspect_mode"] = (EngineComponentId.OrthographicCamera, [(ushort)OrthographicCameraField.AspectMode]),
        ["directional_light.color"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ColorR, (ushort)DirectionalLightField.ColorG, (ushort)DirectionalLightField.ColorB]),
        ["directional_light.intensity_lux"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.IntensityLux]),
        ["directional_light.atmosphere_light_slot"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.AtmosphereLightSlot]),
        ["directional_light.use_per_pixel_atmosphere_transmittance"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.UsePerPixelAtmosphereTransmittance]),
        ["directional_light.casts_shadows"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CastsShadows]),
        ["directional_light.affects_world"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.AffectsWorld]),
        ["directional_light.angular_size_radians"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.AngularSizeRadians]),
        ["directional_light.exposure_compensation"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ExposureCompensation]),
        ["directional_light.atmosphere_disk_luminance_scale_rgb"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.DiskScaleR, (ushort)DirectionalLightField.DiskScaleG, (ushort)DirectionalLightField.DiskScaleB]),
        ["directional_light.shadow.bias"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ShadowBias]),
        ["directional_light.shadow.normal_bias"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ShadowNormalBias]),
        ["directional_light.shadow.contact_shadows"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ContactShadows]),
        ["directional_light.shadow.resolution_hint"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.ShadowResolutionHint]),
        ["directional_light.cascade_count"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CascadeCount]),
        ["directional_light.split_mode"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.SplitMode]),
        ["directional_light.max_shadow_distance"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.MaxShadowDistance]),
        ["directional_light.cascade_distance_0"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CascadeDistance0]),
        ["directional_light.cascade_distance_1"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CascadeDistance1]),
        ["directional_light.cascade_distance_2"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CascadeDistance2]),
        ["directional_light.cascade_distance_3"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.CascadeDistance3]),
        ["directional_light.distribution_exponent"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.DistributionExponent]),
        ["directional_light.transition_fraction"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.TransitionFraction]),
        ["directional_light.distance_fadeout_fraction"] = (EngineComponentId.DirectionalLight, [(ushort)DirectionalLightField.DistanceFadeoutFraction]),
        ["local_fog_volume.enabled"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.Enabled]),
        ["local_fog_volume.radial_fog_extinction"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.RadialFogExtinction]),
        ["local_fog_volume.height_fog_extinction"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.HeightFogExtinction]),
        ["local_fog_volume.height_fog_falloff"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.HeightFogFalloff]),
        ["local_fog_volume.height_fog_offset"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.HeightFogOffset]),
        ["local_fog_volume.fog_phase_g"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.FogPhaseG]),
        ["local_fog_volume.fog_albedo"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.FogAlbedoR, (ushort)LocalFogVolumeField.FogAlbedoG, (ushort)LocalFogVolumeField.FogAlbedoB]),
        ["local_fog_volume.fog_emissive"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.FogEmissiveR, (ushort)LocalFogVolumeField.FogEmissiveG, (ushort)LocalFogVolumeField.FogEmissiveB]),
        ["local_fog_volume.sort_priority"] = (EngineComponentId.LocalFogVolume, [(ushort)LocalFogVolumeField.SortPriority]),
        ["node.visible"] = (EngineComponentId.Node, [(ushort)NodeField.Visible]),
        ["node.casts_shadows"] = (EngineComponentId.Node, [(ushort)NodeField.CastsShadows]),
        ["node.receives_shadows"] = (EngineComponentId.Node, [(ushort)NodeField.ReceivesShadows]),
    }.ToFrozenDictionary(StringComparer.Ordinal);

    // Point and spot lights share their keys; the component kind chooses the engine component.
    private static readonly FrozenDictionary<string, ushort[]> LocalLight = new Dictionary<string, ushort[]>(StringComparer.Ordinal)
    {
        ["light.color"] = [(ushort)LocalLightField.ColorR, (ushort)LocalLightField.ColorG, (ushort)LocalLightField.ColorB],
        ["light.affects_world"] = [(ushort)LocalLightField.AffectsWorld],
        ["light.casts_shadows"] = [(ushort)LocalLightField.CastsShadows],
        ["light.exposure_compensation"] = [(ushort)LocalLightField.ExposureCompensation],
        ["light.shadow.bias"] = [(ushort)LocalLightField.ShadowBias],
        ["light.shadow.normal_bias"] = [(ushort)LocalLightField.ShadowNormalBias],
        ["light.shadow.contact_shadows"] = [(ushort)LocalLightField.ContactShadows],
        ["light.shadow.resolution_hint"] = [(ushort)LocalLightField.ShadowResolutionHint],
        ["light.luminous_flux_lm"] = [(ushort)LocalLightField.LuminousFluxLumens],
        ["light.range"] = [(ushort)LocalLightField.Range],
        ["light.source_radius"] = [(ushort)LocalLightField.SourceRadius],
        ["light.inner_cone_angle_radians"] = [(ushort)LocalLightField.InnerConeAngleRadians],
        ["light.outer_cone_angle_radians"] = [(ushort)LocalLightField.OuterConeAngleRadians],
    }.ToFrozenDictionary(StringComparer.Ordinal);

    /// <summary>Gets the engine mapping of a property.</summary>
    /// <param name="componentKind">The property's component kind.</param>
    /// <param name="engineCommandKey">The property's engine command key.</param>
    /// <returns>The mapping, or <see langword="null"/> when the engine takes the property another way.</returns>
    public static EngineFieldMapping? For(string componentKind, string engineCommandKey)
    {
        if (componentKind is SceneProperties.PointLightKind or SceneProperties.SpotLightKind)
        {
            var component = componentKind == SceneProperties.PointLightKind ? EngineComponentId.PointLight : EngineComponentId.SpotLight;
            return LocalLight.TryGetValue(engineCommandKey, out var lightFields) ? EngineFieldMapping.Of((ushort)component, lightFields) : null;
        }

        if (!Shared.TryGetValue(engineCommandKey, out var mapping))
        {
            return null;
        }

        var result = EngineFieldMapping.Of((ushort)mapping.Component, mapping.Fields);

        // The inspector edits the vertical field of view in degrees; the engine takes radians.
        return engineCommandKey == "perspective_camera.field_of_view_degrees"
            ? result with { Encode = static value => [(float)value! * (MathF.PI / 180f)] }
            : result;
    }
}
