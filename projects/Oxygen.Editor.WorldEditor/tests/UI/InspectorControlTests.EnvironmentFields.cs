// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests;

/// <summary>Declares control values and independent source/native expectations for environment fields.</summary>
public sealed partial class InspectorControlTests
{
    private static readonly EnvironmentFieldCase[] NativeEnvironmentFields =
    [
        new("AtmosphereEnabled", ControlValue: false, ExpectedValue: false, source => source.AtmosphereEnabled, state => state.AtmosphereEnabled),
        new("SunDiskEnabled", ControlValue: false, ExpectedValue: false, source => source.SkyAtmosphere.SunDiskEnabled, state => state.SunDiskEnabled),
        new("PlanetRadiusKm", 6_400f, 6_400_000f, source => source.SkyAtmosphere.PlanetRadiusMeters, state => state.PlanetRadiusMeters),
        new("AtmosphereHeightKm", 90f, 90_000f, source => source.SkyAtmosphere.AtmosphereHeightMeters, state => state.AtmosphereHeightMeters),
        new("GroundAlbedoR", 0.15f, 0.15f, source => source.SkyAtmosphere.GroundAlbedoRgb.X, state => state.GroundAlbedoRgb.X, VectorTag: "GroundAlbedo", VectorAxis: "X"),
        new("GroundAlbedoG", 0.25f, 0.25f, source => source.SkyAtmosphere.GroundAlbedoRgb.Y, state => state.GroundAlbedoRgb.Y, VectorTag: "GroundAlbedo", VectorAxis: "Y"),
        new("GroundAlbedoB", 0.35f, 0.35f, source => source.SkyAtmosphere.GroundAlbedoRgb.Z, state => state.GroundAlbedoRgb.Z, VectorTag: "GroundAlbedo", VectorAxis: "Z"),
        new("RayleighScaleHeightKm", 7f, 7_000f, source => source.SkyAtmosphere.RayleighScaleHeightMeters, state => state.RayleighScaleHeightMeters),
        new("MieScaleHeightKm", 1.4f, 1_400f, source => source.SkyAtmosphere.MieScaleHeightMeters, state => state.MieScaleHeightMeters),
        new("MieAnisotropy", 0.7f, 0.7f, source => source.SkyAtmosphere.MieAnisotropy, state => state.MieAnisotropy),
        new("SkyLuminanceR", 0.15f, 0.15f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.X, state => state.SkyLuminanceFactorRgb.X, VectorTag: "SkyLuminance", VectorAxis: "X"),
        new("SkyLuminanceG", 0.25f, 0.25f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Y, state => state.SkyLuminanceFactorRgb.Y, VectorTag: "SkyLuminance", VectorAxis: "Y"),
        new("SkyLuminanceB", 0.35f, 0.35f, source => source.SkyAtmosphere.SkyLuminanceFactorRgb.Z, state => state.SkyLuminanceFactorRgb.Z, VectorTag: "SkyLuminance", VectorAxis: "Z"),
        new("AerialPerspectiveDistanceScale", 1.2f, 1.2f, source => source.SkyAtmosphere.AerialPerspectiveDistanceScale, state => state.AerialPerspectiveDistanceScale),
        new("AerialScatteringStrength", 0.8f, 0.8f, source => source.SkyAtmosphere.AerialScatteringStrength, state => state.AerialScatteringStrength),
        new("AerialPerspectiveStartDepthMeters", 40f, 40f, source => source.SkyAtmosphere.AerialPerspectiveStartDepthMeters, state => state.AerialPerspectiveStartDepthMeters),
        new("HeightFogContribution", 0.6f, 0.6f, source => source.SkyAtmosphere.HeightFogContribution, state => state.HeightFogContribution),
        new("ExposureMode", ExposureMode.ManualCamera, ExposureMode.ManualCamera, source => source.PostProcess.ExposureMode, state => (ExposureMode)state.ExposureMode, Automatic: true),
        new("ExposureEnabled", ControlValue: false, ExpectedValue: false, source => source.PostProcess.ExposureEnabled, state => state.ExposureEnabled),
        new("ExposureKey", 11f, 11f, source => source.PostProcess.ExposureKey, state => state.ExposureKey),
        new("ManualExposureEv", 5.5f, 5.5f, source => source.PostProcess.ManualExposureEv, state => state.ManualExposureEv),
        new("ExposureCompensation", 1.25f, 1.25f, source => source.PostProcess.ExposureCompensationEv, state => state.ExposureCompensation),
        new("ToneMapping", ToneMappingMode.Reinhard, ToneMappingMode.Reinhard, source => source.PostProcess.ToneMapper, state => (ToneMappingMode)state.ToneMapping),
        new("AutoExposureMeteringMode", MeteringMode.Spot, MeteringMode.Spot, source => source.PostProcess.AutoExposureMeteringMode, state => (MeteringMode)state.AutoExposureMeteringMode, Automatic: true),
        new("AutoExposureMinEv", -3f, -3f, source => source.PostProcess.AutoExposureMinEv, state => state.AutoExposureMinEv, Automatic: true),
        new("AutoExposureMaxEv", 14f, 14f, source => source.PostProcess.AutoExposureMaxEv, state => state.AutoExposureMaxEv, Automatic: true),
        new("AutoExposureSpeedUp", 4f, 4f, source => source.PostProcess.AutoExposureSpeedUp, state => state.AutoExposureSpeedUp, Automatic: true),
        new("AutoExposureSpeedDown", 2f, 2f, source => source.PostProcess.AutoExposureSpeedDown, state => state.AutoExposureSpeedDown, Automatic: true),
        new("AutoExposureLowPercentile", 0.2f, 0.2f, source => source.PostProcess.AutoExposureLowPercentile, state => state.AutoExposureLowPercentile, Automatic: true),
        new("AutoExposureHighPercentile", 0.8f, 0.8f, source => source.PostProcess.AutoExposureHighPercentile, state => state.AutoExposureHighPercentile, Automatic: true),
        new("AutoExposureMinLogLuminance", -10f, -10f, source => source.PostProcess.AutoExposureMinLogLuminance, state => state.AutoExposureMinLogLuminance, Automatic: true),
        new("AutoExposureLogLuminanceRange", 20f, 20f, source => source.PostProcess.AutoExposureLogLuminanceRange, state => state.AutoExposureLogLuminanceRange, Automatic: true),
        new("AutoExposureTargetLuminance", 0.25f, 0.25f, source => source.PostProcess.AutoExposureTargetLuminance, state => state.AutoExposureTargetLuminance, Automatic: true),
        new("AutoExposureSpotMeterRadius", 0.4f, 0.4f, source => source.PostProcess.AutoExposureSpotMeterRadius, state => state.AutoExposureSpotMeterRadius, Automatic: true),
        new("BloomIntensity", 0.7f, 0.7f, source => source.PostProcess.BloomIntensity, state => state.BloomIntensity),
        new("BloomThreshold", 1.5f, 1.5f, source => source.PostProcess.BloomThreshold, state => state.BloomThreshold),
        new("Saturation", 0.9f, 0.9f, source => source.PostProcess.Saturation, state => state.Saturation),
        new("Contrast", 1.1f, 1.1f, source => source.PostProcess.Contrast, state => state.Contrast),
        new("VignetteIntensity", 0.3f, 0.3f, source => source.PostProcess.VignetteIntensity, state => state.VignetteIntensity),
        new("DisplayGamma", 2.4f, 2.4f, source => source.PostProcess.DisplayGamma, state => state.DisplayGamma),
    ];

    private sealed record EnvironmentFieldCase(
        string Field,
        object ControlValue,
        object ExpectedValue,
        Func<SceneEnvironmentData, object> ReadSource,
        Func<RuntimeEnvironmentState, object> ReadNative,
        bool Automatic = false,
        string? VectorTag = null,
        string? VectorAxis = null);
}
