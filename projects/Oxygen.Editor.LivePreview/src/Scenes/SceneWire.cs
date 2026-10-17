// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Interop;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Where each scene property goes in the engine: its component, its fields and how its value encodes.</summary>
/// <remarks>
/// The field ids are the editor module's wire vocabulary, which Interop publishes from the native constants. Geometry and
/// the scene environment have no entry: the engine takes a geometry asset with its component and the environment as a
/// whole.
/// </remarks>
internal static class SceneWire
{
    private static readonly FrozenDictionary<PropertyId, Mapping> Mappings = Build();

    /// <summary>Gets every property the engine takes as fields.</summary>
    public static IEnumerable<PropertyId> Properties => Mappings.Keys;

    /// <summary>Gets the engine mapping of a property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="mapping">The mapping, when the engine takes the property as fields.</param>
    /// <returns><see langword="true"/> when the property has a mapping.</returns>
    public static bool TryGet(PropertyId property, out Mapping mapping) => Mappings.TryGetValue(property, out mapping!);

    /// <summary>Gets a value indicating whether a node has the component that an engine component id addresses.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The engine component.</param>
    /// <returns><see langword="true"/> when the node has the component.</returns>
    public static bool HasComponent(SceneNode node, EngineComponentId component)
    {
        ArgumentNullException.ThrowIfNull(node);
        return component switch
        {
            EngineComponentId.Transform => node.Components.Any(static value => value is TransformComponent),
            EngineComponentId.PerspectiveCamera => node.Components.Any(static value => value is PerspectiveCamera),
            EngineComponentId.OrthographicCamera => node.Components.Any(static value => value is OrthographicCamera),
            EngineComponentId.DirectionalLight => node.Components.Any(static value => value is DirectionalLightComponent),
            EngineComponentId.PointLight => node.Components.Any(static value => value is PointLightComponent),
            EngineComponentId.SpotLight => node.Components.Any(static value => value is SpotLightComponent),
            EngineComponentId.LocalFogVolume => node.Components.Any(static value => value is LocalFogVolumeComponent),
            EngineComponentId.Node => true,
            _ => false,
        };
    }

    private static FrozenDictionary<PropertyId, Mapping> Build()
    {
        var map = new Dictionary<PropertyId, Mapping>();
        void Add(string kind, string pointer, EngineComponentId component, params ushort[] fields)
            => map.Add(new PropertyId(kind, pointer), new Mapping(component, [.. fields]));

        AddTransform(Add);
        AddCameras(Add, map);
        AddDirectionalLight(Add);
        AddLocalLight(Add, SceneProperties.PointLightKind, EngineComponentId.PointLight);
        AddLocalLight(Add, SceneProperties.SpotLightKind, EngineComponentId.SpotLight);
        AddLocalFogVolume(Add);
        AddNodeRendering(Add);
        return map.ToFrozenDictionary();
    }

    private static void AddTransform(Action<string, string, EngineComponentId, ushort[]> add)
    {
        const string kind = SceneProperties.TransformKind;
        const EngineComponentId component = EngineComponentId.Transform;
        add(kind, "/local_position/0", component, [(ushort)TransformField.PositionX]);
        add(kind, "/local_position/1", component, [(ushort)TransformField.PositionY]);
        add(kind, "/local_position/2", component, [(ushort)TransformField.PositionZ]);
        add(kind, "/local_rotation_euler_degrees/0", component, [(ushort)TransformField.RotationXDegrees]);
        add(kind, "/local_rotation_euler_degrees/1", component, [(ushort)TransformField.RotationYDegrees]);
        add(kind, "/local_rotation_euler_degrees/2", component, [(ushort)TransformField.RotationZDegrees]);
        add(kind, "/local_scale/0", component, [(ushort)TransformField.ScaleX]);
        add(kind, "/local_scale/1", component, [(ushort)TransformField.ScaleY]);
        add(kind, "/local_scale/2", component, [(ushort)TransformField.ScaleZ]);
    }

    private static void AddCameras(Action<string, string, EngineComponentId, ushort[]> add, Dictionary<PropertyId, Mapping> map)
    {
        const string perspective = SceneProperties.PerspectiveCameraKind;
        var component = EngineComponentId.PerspectiveCamera;

        // The inspector edits the vertical field of view in degrees; the engine takes radians.
        map.Add(
            new PropertyId(perspective, "/field_of_view_degrees"),
            new Mapping(component, [(ushort)PerspectiveCameraField.FieldOfViewYRadians]) { Encode = static value => [(float)value! * (MathF.PI / 180f)] });
        add(perspective, "/aspect_ratio", component, [(ushort)PerspectiveCameraField.AspectRatio]);
        add(perspective, "/near_plane", component, [(ushort)PerspectiveCameraField.NearPlane]);
        add(perspective, "/far_plane", component, [(ushort)PerspectiveCameraField.FarPlane]);
        add(perspective, "/aperture_f", component, [(ushort)PerspectiveCameraField.ApertureF]);
        add(perspective, "/shutter_rate", component, [(ushort)PerspectiveCameraField.ShutterRate]);
        add(perspective, "/iso", component, [(ushort)PerspectiveCameraField.Iso]);
        add(perspective, "/aspect_mode", component, [(ushort)PerspectiveCameraField.AspectMode]);

        const string orthographic = SceneProperties.OrthographicCameraKind;
        component = EngineComponentId.OrthographicCamera;
        add(orthographic, "/orthographic_size", component, [(ushort)OrthographicCameraField.OrthographicSize]);
        add(orthographic, "/aspect_ratio", component, [(ushort)OrthographicCameraField.AspectRatio]);
        add(orthographic, "/near_plane", component, [(ushort)OrthographicCameraField.NearPlane]);
        add(orthographic, "/far_plane", component, [(ushort)OrthographicCameraField.FarPlane]);
        add(orthographic, "/aperture_f", component, [(ushort)OrthographicCameraField.ApertureF]);
        add(orthographic, "/shutter_rate", component, [(ushort)OrthographicCameraField.ShutterRate]);
        add(orthographic, "/iso", component, [(ushort)OrthographicCameraField.Iso]);
        add(orthographic, "/aspect_mode", component, [(ushort)OrthographicCameraField.AspectMode]);
    }

    private static void AddDirectionalLight(Action<string, string, EngineComponentId, ushort[]> add)
    {
        const string kind = SceneProperties.DirectionalLightKind;
        const EngineComponentId component = EngineComponentId.DirectionalLight;
        add(kind, "/color", component, [(ushort)DirectionalLightField.ColorR, (ushort)DirectionalLightField.ColorG, (ushort)DirectionalLightField.ColorB]);
        add(kind, "/intensity_lux", component, [(ushort)DirectionalLightField.IntensityLux]);
        add(kind, "/atmosphere_light_slot", component, [(ushort)DirectionalLightField.AtmosphereLightSlot]);
        add(kind, "/use_per_pixel_atmosphere_transmittance", component, [(ushort)DirectionalLightField.UsePerPixelAtmosphereTransmittance]);
        add(kind, "/casts_shadows", component, [(ushort)DirectionalLightField.CastsShadows]);
        add(kind, "/affects_world", component, [(ushort)DirectionalLightField.AffectsWorld]);
        add(kind, "/angular_size_radians", component, [(ushort)DirectionalLightField.AngularSizeRadians]);
        add(kind, "/exposure_compensation", component, [(ushort)DirectionalLightField.ExposureCompensation]);
        add(kind, "/atmosphere_disk_luminance_scale_rgb", component, [(ushort)DirectionalLightField.DiskScaleR, (ushort)DirectionalLightField.DiskScaleG, (ushort)DirectionalLightField.DiskScaleB]);
        add(kind, "/shadow/bias", component, [(ushort)DirectionalLightField.ShadowBias]);
        add(kind, "/shadow/normal_bias", component, [(ushort)DirectionalLightField.ShadowNormalBias]);
        add(kind, "/shadow/contact_shadows", component, [(ushort)DirectionalLightField.ContactShadows]);
        add(kind, "/shadow/resolution_hint", component, [(ushort)DirectionalLightField.ShadowResolutionHint]);
        add(kind, "/csm/cascade_count", component, [(ushort)DirectionalLightField.CascadeCount]);
        add(kind, "/csm/split_mode", component, [(ushort)DirectionalLightField.SplitMode]);
        add(kind, "/csm/max_shadow_distance", component, [(ushort)DirectionalLightField.MaxShadowDistance]);
        add(kind, "/csm/cascade_distances/0", component, [(ushort)DirectionalLightField.CascadeDistance0]);
        add(kind, "/csm/cascade_distances/1", component, [(ushort)DirectionalLightField.CascadeDistance1]);
        add(kind, "/csm/cascade_distances/2", component, [(ushort)DirectionalLightField.CascadeDistance2]);
        add(kind, "/csm/cascade_distances/3", component, [(ushort)DirectionalLightField.CascadeDistance3]);
        add(kind, "/csm/distribution_exponent", component, [(ushort)DirectionalLightField.DistributionExponent]);
        add(kind, "/csm/transition_fraction", component, [(ushort)DirectionalLightField.TransitionFraction]);
        add(kind, "/csm/distance_fadeout_fraction", component, [(ushort)DirectionalLightField.DistanceFadeoutFraction]);
    }

    // Point and spot lights share their fields; the component kind chooses the engine component.
    private static void AddLocalLight(Action<string, string, EngineComponentId, ushort[]> add, string kind, EngineComponentId component)
    {
        add(kind, "/color", component, [(ushort)LocalLightField.ColorR, (ushort)LocalLightField.ColorG, (ushort)LocalLightField.ColorB]);
        add(kind, "/affects_world", component, [(ushort)LocalLightField.AffectsWorld]);
        add(kind, "/casts_shadows", component, [(ushort)LocalLightField.CastsShadows]);
        add(kind, "/exposure_compensation", component, [(ushort)LocalLightField.ExposureCompensation]);
        add(kind, "/shadow/bias", component, [(ushort)LocalLightField.ShadowBias]);
        add(kind, "/shadow/normal_bias", component, [(ushort)LocalLightField.ShadowNormalBias]);
        add(kind, "/shadow/contact_shadows", component, [(ushort)LocalLightField.ContactShadows]);
        add(kind, "/shadow/resolution_hint", component, [(ushort)LocalLightField.ShadowResolutionHint]);
        add(kind, "/luminous_flux_lm", component, [(ushort)LocalLightField.LuminousFluxLumens]);
        add(kind, "/range", component, [(ushort)LocalLightField.Range]);
        add(kind, "/source_radius", component, [(ushort)LocalLightField.SourceRadius]);
        if (component == EngineComponentId.SpotLight)
        {
            add(kind, "/inner_cone_angle_radians", component, [(ushort)LocalLightField.InnerConeAngleRadians]);
            add(kind, "/outer_cone_angle_radians", component, [(ushort)LocalLightField.OuterConeAngleRadians]);
        }
    }

    private static void AddLocalFogVolume(Action<string, string, EngineComponentId, ushort[]> add)
    {
        const string kind = SceneProperties.LocalFogVolumeKind;
        const EngineComponentId component = EngineComponentId.LocalFogVolume;
        add(kind, "/enabled", component, [(ushort)LocalFogVolumeField.Enabled]);
        add(kind, "/radial_fog_extinction", component, [(ushort)LocalFogVolumeField.RadialFogExtinction]);
        add(kind, "/height_fog_extinction", component, [(ushort)LocalFogVolumeField.HeightFogExtinction]);
        add(kind, "/height_fog_falloff", component, [(ushort)LocalFogVolumeField.HeightFogFalloff]);
        add(kind, "/height_fog_offset", component, [(ushort)LocalFogVolumeField.HeightFogOffset]);
        add(kind, "/fog_phase_g", component, [(ushort)LocalFogVolumeField.FogPhaseG]);
        add(kind, "/fog_albedo", component, [(ushort)LocalFogVolumeField.FogAlbedoR, (ushort)LocalFogVolumeField.FogAlbedoG, (ushort)LocalFogVolumeField.FogAlbedoB]);
        add(kind, "/fog_emissive", component, [(ushort)LocalFogVolumeField.FogEmissiveR, (ushort)LocalFogVolumeField.FogEmissiveG, (ushort)LocalFogVolumeField.FogEmissiveB]);
        add(kind, "/sort_priority", component, [(ushort)LocalFogVolumeField.SortPriority]);
    }

    private static void AddNodeRendering(Action<string, string, EngineComponentId, ushort[]> add)
    {
        const string kind = SceneProperties.NodeRenderingKind;
        const EngineComponentId component = EngineComponentId.Node;
        add(kind, "/visible", component, [(ushort)NodeField.Visible]);
        add(kind, "/casts_shadows", component, [(ushort)NodeField.CastsShadows]);
        add(kind, "/receives_shadows", component, [(ushort)NodeField.ReceivesShadows]);
    }

    /// <summary>A property's engine component and fields, one field per encoded value.</summary>
    /// <remarks>
    /// The engine takes every property as floats. A scalar maps to one field; a vector maps to one field per axis. The
    /// default encoding writes floats and integers as they are, booleans as one or zero and enumerations as their numeric
    /// value; <see cref="Encode"/> replaces it when the engine expects another unit.
    /// </remarks>
    /// <param name="Component">The engine component.</param>
    /// <param name="Fields">The engine fields, one per encoded value.</param>
    internal sealed record Mapping(EngineComponentId Component, ImmutableArray<ushort> Fields)
    {
        /// <summary>Gets the encoding of a value into one float per field, or <see langword="null"/> for the default.</summary>
        public Func<object?, ImmutableArray<float>>? Encode { get; init; }

        /// <summary>Encodes a value into one float per field.</summary>
        /// <param name="value">The property's value.</param>
        /// <returns>The encoded values, in field order.</returns>
        /// <exception cref="InvalidOperationException">The value does not encode into one float per field.</exception>
        public ImmutableArray<float> EncodeValue(object? value)
        {
            var encoded = this.Encode?.Invoke(value) ?? EncodeDefault(value);
            return encoded.Length == this.Fields.Length
                ? encoded
                : throw new InvalidOperationException($"A value of type {value?.GetType().Name ?? "null"} encodes into {encoded.Length} floats, not {this.Fields.Length}.");
        }

        private static ImmutableArray<float> EncodeDefault(object? value)
            => value switch
            {
                float scalar => [scalar],
                int integer => [integer],
                bool flag => [flag ? 1f : 0f],
                Enum enumeration => [Convert.ToInt32(enumeration, CultureInfo.InvariantCulture)],
                Vector3 vector => [vector.X, vector.Y, vector.Z],
                Vector4 vector => [vector.X, vector.Y, vector.Z, vector.W],
                null => throw new InvalidOperationException("A null value has no engine encoding."),
                _ => throw new InvalidOperationException($"A value of type {value.GetType().Name} has no default engine encoding."),
            };
    }
}
