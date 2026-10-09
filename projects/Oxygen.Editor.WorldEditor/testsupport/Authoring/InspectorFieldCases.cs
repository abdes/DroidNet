// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorFieldCases
{
    internal static readonly EnvironmentFieldCase[] NativeEnvironmentFields = [new("SunDiskEnabled", ControlValue: false, ExpectedValue: false, source => source.SkyAtmosphere.SunDiskEnabled, state => state.SunDiskEnabled), new("PlanetRadiusKm", 6_400f, 6_400_000f, source => source.SkyAtmosphere.PlanetRadiusMeters, state => state.PlanetRadiusMeters), new("AtmosphereHeightKm", 90f, 90_000f, source => source.SkyAtmosphere.AtmosphereHeightMeters, state => state.AtmosphereHeightMeters), new("GroundAlbedoR", 0.15f, 0.15f, source => source.SkyAtmosphere.GroundAlbedoRgb.X, state => state.GroundAlbedoRgb.X, VectorTag: "GroundAlbedo", VectorAxis: "X"), new("GroundAlbedoG", 0.25f, 0.25f, source => source.SkyAtmosphere.GroundAlbedoRgb.Y, state => state.GroundAlbedoRgb.Y, VectorTag: "GroundAlbedo", VectorAxis: "Y"), new("GroundAlbedoB", 0.35f, 0.35f, source => source.SkyAtmosphere.GroundAlbedoRgb.Z, state => state.GroundAlbedoRgb.Z, VectorTag: "GroundAlbedo", VectorAxis: "Z"), new("RayleighScaleHeightKm", 7f, 7_000f, source => source.SkyAtmosphere.RayleighScaleHeightMeters, state => state.RayleighScaleHeightMeters), new("MieScaleHeightKm", 1.4f, 1_400f, source => source.SkyAtmosphere.MieScaleHeightMeters, state => state.MieScaleHeightMeters), new("MieAnisotropy", 0.7f, 0.7f, source => source.SkyAtmosphere.MieAnisotropy, state => state.MieAnisotropy), new("SkyLuminanceR", 0.15f, 0.15f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.X, state => state.SkyLuminanceFactorRgb.X, VectorTag: "SkyLuminance", VectorAxis: "X"), new("SkyLuminanceG", 0.25f, 0.25f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Y, state => state.SkyLuminanceFactorRgb.Y, VectorTag: "SkyLuminance", VectorAxis: "Y"), new("SkyLuminanceB", 0.35f, 0.35f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Z, state => state.SkyLuminanceFactorRgb.Z, VectorTag: "SkyLuminance", VectorAxis: "Z"), new("AerialPerspectiveDistanceScale", 1.2f, 1.2f, source => source.SkyAtmosphere.AerialPerspectiveDistanceScale, state => state.AerialPerspectiveDistanceScale), new("AerialScatteringStrength", 0.8f, 0.8f, source => source.SkyAtmosphere.AerialScatteringStrength, state => state.AerialScatteringStrength), new("AerialPerspectiveStartDepthMeters", 40f, 40f, source => source.SkyAtmosphere.AerialPerspectiveStartDepthMeters, state => state.AerialPerspectiveStartDepthMeters), new("HeightFogContribution", 0.6f, 0.6f, source => source.SkyAtmosphere.HeightFogContribution, state => state.HeightFogContribution), new("ExposureMode", ExposureMode.ManualCamera, ExposureMode.ManualCamera, source => source.PostProcess.ExposureMode, state => (ExposureMode)state.ExposureMode, Automatic: true), new("ExposureEnabled", ControlValue: false, ExpectedValue: false, source => source.PostProcess.ExposureEnabled, state => state.ExposureEnabled), new("ExposureKey", 11f, 11f, source => source.PostProcess.ExposureKey, state => state.ExposureKey), new("ManualExposureEv", 5.5f, 5.5f, source => source.PostProcess.ManualExposureEv, state => state.ManualExposureEv), new("ExposureCompensation", 1.25f, 1.25f, source => source.PostProcess.ExposureCompensationEv, state => state.ExposureCompensation), new("ToneMapping", ToneMappingMode.Reinhard, ToneMappingMode.Reinhard, source => source.PostProcess.ToneMapper, state => (ToneMappingMode)state.ToneMapping), new("AutoExposureMeteringMode", MeteringMode.Spot, MeteringMode.Spot, source => source.PostProcess.AutoExposureMeteringMode, state => (MeteringMode)state.AutoExposureMeteringMode, Automatic: true), new("AutoExposureMinEv", -3f, -3f, source => source.PostProcess.AutoExposureMinEv, state => state.AutoExposureMinEv, Automatic: true), new("AutoExposureMaxEv", 14f, 14f, source => source.PostProcess.AutoExposureMaxEv, state => state.AutoExposureMaxEv, Automatic: true), new("AutoExposureSpeedUp", 4f, 4f, source => source.PostProcess.AutoExposureSpeedUp, state => state.AutoExposureSpeedUp, Automatic: true), new("AutoExposureSpeedDown", 2f, 2f, source => source.PostProcess.AutoExposureSpeedDown, state => state.AutoExposureSpeedDown, Automatic: true), new("AutoExposureLowPercentile", 0.2f, 0.2f, source => source.PostProcess.AutoExposureLowPercentile, state => state.AutoExposureLowPercentile, Automatic: true), new("AutoExposureHighPercentile", 0.8f, 0.8f, source => source.PostProcess.AutoExposureHighPercentile, state => state.AutoExposureHighPercentile, Automatic: true), new("AutoExposureMinLogLuminance", -10f, -10f, source => source.PostProcess.AutoExposureMinLogLuminance, state => state.AutoExposureMinLogLuminance, Automatic: true), new("AutoExposureLogLuminanceRange", 20f, 20f, source => source.PostProcess.AutoExposureLogLuminanceRange, state => state.AutoExposureLogLuminanceRange, Automatic: true), new("AutoExposureTargetLuminance", 0.25f, 0.25f, source => source.PostProcess.AutoExposureTargetLuminance, state => state.AutoExposureTargetLuminance, Automatic: true), new("AutoExposureSpotMeterRadius", 0.4f, 0.4f, source => source.PostProcess.AutoExposureSpotMeterRadius, state => state.AutoExposureSpotMeterRadius, Automatic: true), new("BloomIntensity", 0.7f, 0.7f, source => source.PostProcess.BloomIntensity, state => state.BloomIntensity), new("BloomThreshold", 1.5f, 1.5f, source => source.PostProcess.BloomThreshold, state => state.BloomThreshold), new("Saturation", 0.9f, 0.9f, source => source.PostProcess.Saturation, state => state.Saturation), new("Contrast", 1.1f, 1.1f, source => source.PostProcess.Contrast, state => state.Contrast), new("VignetteIntensity", 0.3f, 0.3f, source => source.PostProcess.VignetteIntensity, state => state.VignetteIntensity), new("DisplayGamma", 2.4f, 2.4f, source => source.PostProcess.DisplayGamma, state => state.DisplayGamma),];

    internal sealed record EnvironmentFieldCase(string Field, object ControlValue, object ExpectedValue, Func<SceneEnvironmentData, object> ReadSource, Func<RuntimeEnvironmentState, object> ReadNative, bool Automatic = false, string? VectorTag = null, string? VectorAxis = null);

    private static readonly Action<SceneNode> ManualCascadeSplit = static node => SeedManualCascade(node, DirectionalCsmSplitMode.ManualDistances);

    private static readonly Action<SceneNode> ManualStoredFourthBoundary = static node => SeedManualCascade(node, DirectionalCsmSplitMode.ManualDistances, 220f);

    internal static readonly NodeFieldCase[] NativeNodeFields = [new("Transform", "PositionX", 1, 0, 12f, 12f), new("Transform", "PositionY", 1, 1, 12f, 12f), new("Transform", "PositionZ", 1, 2, 12f, 12f), new("Transform", "RotationX", 1, 3, 30f, 30f), new("Transform", "RotationY", 1, 4, 30f, 30f), new("Transform", "RotationZ", 1, 5, 30f, 30f), new("Transform", "ScaleX", 1, 6, 2f, 2f), new("Transform", "ScaleY", 1, 7, 2f, 2f), new("Transform", "ScaleZ", 1, 8, 2f, 2f), new("Camera", "FieldOfView", 2, 0, 75f, 75f * MathF.PI / 180f), new("Camera", "AspectRatio", 2, 1, 1.5f, 1.5f), new("Camera", "NearPlane", 2, 2, 0.5f, 0.5f), new("Camera", "FarPlane", 2, 3, 2000f, 2000f), new("Light", "ColorR", 3, 0, 0.2f, 0.2f), new("Light", "ColorG", 3, 1, 0.3f, 0.3f), new("Light", "ColorB", 3, 2, 0.4f, 0.4f), new("Light", "AffectsWorld", 3, 3, ControlValue: false, 0), new("Light", "DiskScaleR", 3, 27, 2f, 2f), new("Light", "DiskScaleG", 3, 28, 3f, 3f), new("Light", "DiskScaleB", 3, 29, 4f, 4f), new("Light", "CastsShadows", 3, 5, ControlValue: false, 0), new("Light", "ShadowBias", 3, 6, 0.01f, 0.01f), new("Light", "ShadowNormalBias", 3, 7, 0.04f, 0.04f), new("Light", "ContactShadows", 3, 8, ControlValue: true, 1), new("Light", "ShadowResolutionHint", 3, 9, ShadowResolutionHint.High, 2), new("Light", "ExposureCompensation", 3, 10, 1.5f, 1.5f), new("Light", "IntensityLux", 3, 11, 80000f, 80000f), new("Light", "AngularSizeRadians", 3, 12, 0.02f, 0.02f), new("Light", "UsePerPixelAtmosphereTransmittance", 3, 26, ControlValue: true, 1), new("Light", "AtmosphereSlot", 3, 25, AtmosphereLightSlot.Primary, 1), new("Light", "CascadeCount", 3, 15, 3f, 3), new("Light", "SplitMode", 3, 16, DirectionalCsmSplitMode.ManualDistances, 1), new("Light", "MaxShadowDistance", 3, 17, 200f, 200), new("Light", "CascadeDistance1", 3, 18, 12f, 12, ManualCascadeSplit), new("Light", "CascadeDistance2", 3, 19, 32f, 32, ManualCascadeSplit), new("Light", "CascadeDistance3", 3, 20, 90f, 90, ManualCascadeSplit), new("Light", "CascadeDistance4", 3, 21, 220f, 220, ManualStoredFourthBoundary), new("Light", "DistributionExponent", 3, 22, 2f, 2), new("Light", "TransitionFraction", 3, 23, 0.2f, 0.2f), new("Light", "DistanceFadeoutFraction", 3, 24, 0.3f, 0.3f),];

    internal static Dictionary<(ushort component, ushort field), float> SourceNodeProperties(SceneNode node)
    {
        var result = new Dictionary<(ushort component, ushort field), float>();
        var transform = node.Components.OfType<TransformComponent>().Single();
        var position = transform.LocalPosition;
        var rotation = TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation);
        var scale = transform.LocalScale;
        Add(1, [position.X, position.Y, position.Z, rotation.X, rotation.Y, rotation.Z, scale.X, scale.Y, scale.Z]);

        // Node rendering flags (NodeField order); native observation reports them for every node.
        Add(7, [node.IsVisible ? 1f : 0f, node.CastsShadows ? 1f : 0f, node.ReceivesShadows ? 1f : 0f]);
        if (node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } camera)
        {
            Add(2, [camera.FieldOfView * MathF.PI / 180f, camera.AspectRatio, camera.NearPlane, camera.FarPlane, camera.ApertureF, camera.ShutterRate, camera.Iso, (float)camera.AspectMode]);
        }

        if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is { } light)
        {
            Add(3, [light.Color.X, light.Color.Y, light.Color.Z, light.AffectsWorld ? 1f : 0f, 0, light.CastsShadows ? 1f : 0f, light.ShadowBias, light.ShadowNormalBias, light.ContactShadows ? 1f : 0f, (int)light.ShadowResolutionHint, light.ExposureCompensation, light.IntensityLux, light.AngularSizeRadians, 0, 0, light.CascadeCount, (int)light.SplitMode, light.MaxShadowDistance, light.CascadeDistances.X, light.CascadeDistances.Y, light.CascadeDistances.Z, light.CascadeDistances.W, light.DistributionExponent, light.TransitionFraction, light.DistanceFadeoutFraction, (int)light.AtmosphereSlot, light.UsePerPixelAtmosphereTransmittance ? 1f : 0f, light.AtmosphereDiskLuminanceScaleRgb.X, light.AtmosphereDiskLuminanceScaleRgb.Y, light.AtmosphereDiskLuminanceScaleRgb.Z,]);
            foreach (var removed in new ushort[]
            {
                4,
                13,
                14,
            }

            )
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

    /// <summary>Seeds a light in the split mode that makes the edited boundary applicable; the inspector gates boundaries on this precondition.</summary>
    private static void SeedManualCascade(SceneNode node, DirectionalCsmSplitMode splitMode, float? boundary4 = null)
    {
        var light = node.Components.OfType<DirectionalLightComponent>().Single();
        light.SplitMode = splitMode;
        if (boundary4 is { } value)
        {
            light.MaxShadowDistance = 240f;
            light.CascadeDistances = new Vector4(8f, 24f, 64f, value);
        }
    }

    internal sealed record NodeFieldCase(string Kind, string Field, ushort Component, ushort NativeField, object ControlValue, float ExpectedValue, Action<SceneNode>? Arrange = null);
}
