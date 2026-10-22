// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.TestSupport;

/// <summary>The authored fields whose values the native tests compare with the engine's.</summary>
internal static class NativeFieldCases
{
    /// <summary>The environment fields, read from the authored environment and from the engine's.</summary>
    internal static readonly EnvironmentFieldCase[] NativeEnvironmentFields =
    [
        new("SunDiskEnabled", source => source.SkyAtmosphere.SunDiskEnabled, state => state.SunDiskEnabled),
        new("PlanetRadiusMeters", source => source.SkyAtmosphere.PlanetRadiusMeters, state => state.PlanetRadiusMeters),
        new("AtmosphereHeightMeters", source => source.SkyAtmosphere.AtmosphereHeightMeters, state => state.AtmosphereHeightMeters),
        new("GroundAlbedoR", source => source.SkyAtmosphere.GroundAlbedoRgb.X, state => state.GroundAlbedoRgb.X),
        new("GroundAlbedoG", source => source.SkyAtmosphere.GroundAlbedoRgb.Y, state => state.GroundAlbedoRgb.Y),
        new("GroundAlbedoB", source => source.SkyAtmosphere.GroundAlbedoRgb.Z, state => state.GroundAlbedoRgb.Z),
        new("RayleighScaleHeightMeters", source => source.SkyAtmosphere.RayleighScaleHeightMeters, state => state.RayleighScaleHeightMeters),
        new("MieScaleHeightMeters", source => source.SkyAtmosphere.MieScaleHeightMeters, state => state.MieScaleHeightMeters),
        new("MieAnisotropy", source => source.SkyAtmosphere.MieAnisotropy, state => state.MieAnisotropy),
        new("SkyLuminanceR", source => source.SkyAtmosphere.SkyLuminanceFactorRgb.X, state => state.SkyLuminanceFactorRgb.X),
        new("SkyLuminanceG", source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Y, state => state.SkyLuminanceFactorRgb.Y),
        new("SkyLuminanceB", source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Z, state => state.SkyLuminanceFactorRgb.Z),
        new("AerialPerspectiveDistanceScale", source => source.SkyAtmosphere.AerialPerspectiveDistanceScale, state => state.AerialPerspectiveDistanceScale),
        new("AerialScatteringStrength", source => source.SkyAtmosphere.AerialScatteringStrength, state => state.AerialScatteringStrength),
        new("AerialPerspectiveStartDepthMeters", source => source.SkyAtmosphere.AerialPerspectiveStartDepthMeters, state => state.AerialPerspectiveStartDepthMeters),
        new("HeightFogContribution", source => source.SkyAtmosphere.HeightFogContribution, state => state.HeightFogContribution),
        new("ExposureMode", source => source.PostProcess.ExposureMode, state => (ExposureMode)state.ExposureMode),
        new("ExposureEnabled", source => source.PostProcess.ExposureEnabled, state => state.ExposureEnabled),
        new("ExposureKey", source => source.PostProcess.ExposureKey, state => state.ExposureKey),
        new("ManualExposureEv", source => source.PostProcess.ManualExposureEv, state => state.ManualExposureEv),
        new("ExposureCompensation", source => source.PostProcess.ExposureCompensationEv, state => state.ExposureCompensation),
        new("ToneMapping", source => source.PostProcess.ToneMapper, state => (ToneMappingMode)state.ToneMapping),
        new("AutoExposureMeteringMode", source => source.PostProcess.AutoExposureMeteringMode, state => (MeteringMode)state.AutoExposureMeteringMode),
        new("AutoExposureMinEv", source => source.PostProcess.AutoExposureMinEv, state => state.AutoExposureMinEv),
        new("AutoExposureMaxEv", source => source.PostProcess.AutoExposureMaxEv, state => state.AutoExposureMaxEv),
        new("AutoExposureSpeedUp", source => source.PostProcess.AutoExposureSpeedUp, state => state.AutoExposureSpeedUp),
        new("AutoExposureSpeedDown", source => source.PostProcess.AutoExposureSpeedDown, state => state.AutoExposureSpeedDown),
        new("AutoExposureLowPercentile", source => source.PostProcess.AutoExposureLowPercentile, state => state.AutoExposureLowPercentile),
        new("AutoExposureHighPercentile", source => source.PostProcess.AutoExposureHighPercentile, state => state.AutoExposureHighPercentile),
        new("AutoExposureMinLogLuminance", source => source.PostProcess.AutoExposureMinLogLuminance, state => state.AutoExposureMinLogLuminance),
        new("AutoExposureLogLuminanceRange", source => source.PostProcess.AutoExposureLogLuminanceRange, state => state.AutoExposureLogLuminanceRange),
        new("AutoExposureTargetLuminance", source => source.PostProcess.AutoExposureTargetLuminance, state => state.AutoExposureTargetLuminance),
        new("AutoExposureSpotMeterRadius", source => source.PostProcess.AutoExposureSpotMeterRadius, state => state.AutoExposureSpotMeterRadius),
        new("BloomIntensity", source => source.PostProcess.BloomIntensity, state => state.BloomIntensity),
        new("BloomThreshold", source => source.PostProcess.BloomThreshold, state => state.BloomThreshold),
        new("Saturation", source => source.PostProcess.Saturation, state => state.Saturation),
        new("Contrast", source => source.PostProcess.Contrast, state => state.Contrast),
        new("VignetteIntensity", source => source.PostProcess.VignetteIntensity, state => state.VignetteIntensity),
        new("DisplayGamma", source => source.PostProcess.DisplayGamma, state => state.DisplayGamma),
    ];

    /// <summary>The node fields an edit writes, with the native field it reaches and the value the engine reports.</summary>
    internal static readonly NodeFieldCase[] NativeNodeFields =
    [
        new("Transform", "PositionX", 1, 0, Set(SceneSchema.Transform.PositionX, 12f), 12f),
        new("Transform", "RotationY", 1, 4, Set(SceneSchema.Transform.RotationY, 30f), 30f),
        new("Camera", "FieldOfView", 2, 0, Set(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, 75f), 75f * MathF.PI / 180f),
        new("Light", "ColorR", 3, 0, static node => PropertyEdit.SingleEdit(SceneSchema.DirectionalLight.Color, LightOf(node).Color with { X = 0.2f }), 0.2f),
        new("Light", "CastsShadows", 3, 5, Set(SceneSchema.DirectionalLight.CastsShadows, value: false), 0),
        new("Light", "IntensityLux", 3, 11, Set(SceneSchema.DirectionalLight.IntensityLux, 80000f), 80000f),
        new("Light", "CascadeCount", 3, 15, Set(SceneSchema.DirectionalLight.CascadeCount, 3), 3),
        new("Light", "SplitMode", 3, 16, Set(SceneSchema.DirectionalLight.SplitMode, DirectionalCsmSplitMode.ManualDistances), 1),
        new("Node", "Static", 7, 3, Set(SceneSchema.Node.IsStatic, true), 1f),
        new("Node", "RayCastingSelectable", 7, 4, Set(SceneSchema.Node.IsRayCastingSelectable, false), 0f),
        new("Node", "IgnoreParentTransform", 7, 5, Set(SceneSchema.Node.IgnoreParentTransform, true), 1f),
    ];

    /// <summary>Reads a node's properties as the engine observes them: by component and field, as numbers.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The values, by native component and field.</returns>
    internal static Dictionary<(ushort component, ushort field), float> SourceNodeProperties(SceneNode node)
    {
        var result = new Dictionary<(ushort component, ushort field), float>();
        var transform = node.Transform;
        var position = transform.LocalPosition;
        var rotation = TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation);
        var scale = transform.LocalScale;
        Add(1, [position.X, position.Y, position.Z, rotation.X, rotation.Y, rotation.Z, scale.X, scale.Y, scale.Z]);

        // Node flags (NodeField order); native observation reports them for every node.
        var flags = node.Flags;
        Add(7, [flags.IsVisible ? 1f : 0f, flags.CastsShadows ? 1f : 0f, flags.ReceivesShadows ? 1f : 0f, flags.IsStatic ? 1f : 0f, flags.IsRayCastingSelectable ? 1f : 0f, flags.IgnoreParentTransform ? 1f : 0f]);
        if (node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } camera)
        {
            Add(2, [camera.FieldOfView * MathF.PI / 180f, camera.AspectRatio, camera.NearPlane, camera.FarPlane, camera.ApertureF, camera.ShutterRate, camera.Iso, (float)camera.AspectMode]);
        }

        if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is { } light)
        {
            Add(3, [light.Color.X, light.Color.Y, light.Color.Z, light.AffectsWorld ? 1f : 0f, 0, light.CastsShadows ? 1f : 0f, light.ShadowBias, light.ShadowNormalBias, light.ContactShadows ? 1f : 0f, (int)light.ShadowResolutionHint, light.ExposureCompensation, light.IntensityLux, light.AngularSizeRadians, 0, 0, light.CascadeCount, (int)light.SplitMode, light.MaxShadowDistance, light.CascadeDistances.X, light.CascadeDistances.Y, light.CascadeDistances.Z, light.CascadeDistances.W, light.DistributionExponent, light.TransitionFraction, light.DistanceFadeoutFraction, (int)light.AtmosphereSlot, light.UsePerPixelAtmosphereTransmittance ? 1f : 0f, light.AtmosphereDiskLuminanceScaleRgb.X, light.AtmosphereDiskLuminanceScaleRgb.Y, light.AtmosphereDiskLuminanceScaleRgb.Z,]);
            foreach (var removed in new ushort[] { 4, 13, 14 })
            {
                result.Remove((3, removed));
            }
        }

        return result;
        void Add(ushort component, float[] values)
        {
            for (ushort index = 0; index < values.Length; index++)
            {
                result[(component, index)] = values[index];
            }
        }
    }

    private static Func<SceneNode, PropertyEdit> Set<T>(PropertyId<T> id, T value) => _ => PropertyEdit.SingleEdit(id, value);

    private static DirectionalLightComponent LightOf(SceneNode node) => node.Components.OfType<DirectionalLightComponent>().Single();

    /// <summary>An environment field.</summary>
    /// <param name="Field">The field's name in assertion messages.</param>
    /// <param name="ReadSource">Reads the field from the authored environment.</param>
    /// <param name="ReadNative">Reads the field from the engine's environment.</param>
    internal sealed record EnvironmentFieldCase(string Field, Func<SceneEnvironmentData, object> ReadSource, Func<RuntimeEnvironmentState, object> ReadNative);

    /// <summary>A node field an edit writes.</summary>
    /// <param name="Kind">The node's seeded kind: Transform, Camera, Light or Node.</param>
    /// <param name="Field">The field's name.</param>
    /// <param name="Component">The native component.</param>
    /// <param name="NativeField">The native field.</param>
    /// <param name="Edit">Builds the property edit from the node's current values.</param>
    /// <param name="ExpectedValue">The value the engine reports once the edit applies.</param>
    internal sealed record NodeFieldCase(string Kind, string Field, ushort Component, ushort NativeField, Func<SceneNode, PropertyEdit> Edit, float ExpectedValue);
}
