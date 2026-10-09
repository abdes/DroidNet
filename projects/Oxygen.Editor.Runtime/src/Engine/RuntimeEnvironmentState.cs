// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Native environment values and the preceding rendered sky-light snapshot, read at a scene mutation boundary.</summary>
public sealed record RuntimeEnvironmentState
{
    /// <summary>Gets a value indicating whether the native scene owns an environment.</summary>
    public bool Exists { get; init; }

    /// <summary>Gets a value indicating whether atmosphere values are present, including a disabled atmosphere.</summary>
    public bool AtmosphereExists { get; init; }

    /// <summary>Gets a value indicating whether post-process values are present.</summary>
    public bool PostProcessExists { get; init; }

    /// <summary>Gets a value indicating whether this scene has a rendered sky-light snapshot.</summary>
    public bool SkyLightObserved { get; init; }

    /// <summary>Gets a value indicating whether the rendered sky light is enabled.</summary>
    public bool SkyLightEnabled { get; init; }

    /// <summary>Gets a value indicating whether complete sky-light products were accepted for GPU-ordered consumption.</summary>
    public bool SkyLightUsable { get; init; }

    /// <summary>Gets why the rendered sky light has no image-based lighting.</summary>
    public RuntimeSkyLightUnavailableReason SkyLightUnavailableReason { get; init; }

    /// <summary>Gets a value indicating whether neither atmosphere nor captured height fog supplies radiance.</summary>
    public bool SkyLightEmptyCapture { get; init; }

    /// <summary>Gets the native scene lifetime owning the rendered sky-light snapshot.</summary>
    public ulong SkyLightSceneLifetime { get; init; }

    /// <summary>Gets the last rendered frame, preceding this observation's mutation boundary.</summary>
    public ulong SkyLightFrameSequence { get; init; }

    /// <summary>Gets the published sky-light product generation, or zero when unavailable.</summary>
    public uint SkyLightPublishedRevision { get; init; }

    /// <summary>Gets the source identity used by the published products.</summary>
    public ulong SkyLightPublishedSourceRevision { get; init; }

    /// <summary>Gets the desired source identity from the last render decision.</summary>
    public ulong SkyLightDesiredSourceRevision { get; init; }

    /// <summary>Gets snapshot age in frames while updating; zero when current.</summary>
    public ulong SkyLightSourceAgeFrames { get; init; }

    /// <summary>Gets a value indicating whether native atmosphere is enabled.</summary>
    public bool AtmosphereEnabled { get; init; }

    /// <summary>Gets a value indicating whether the native atmosphere draws a sun disk.</summary>
    public bool SunDiskEnabled { get; init; }

    /// <summary>Gets the observed PlanetRadiusMeters value.</summary>
    public float PlanetRadiusMeters { get; init; }

    /// <summary>Gets the observed AtmosphereHeightMeters value.</summary>
    public float AtmosphereHeightMeters { get; init; }

    /// <summary>Gets the observed GroundAlbedoRgb value.</summary>
    public Vector3 GroundAlbedoRgb { get; init; }

    /// <summary>Gets the observed RayleighScaleHeightMeters value.</summary>
    public float RayleighScaleHeightMeters { get; init; }

    /// <summary>Gets the observed MieScaleHeightMeters value.</summary>
    public float MieScaleHeightMeters { get; init; }

    /// <summary>Gets the observed MieAnisotropy value.</summary>
    public float MieAnisotropy { get; init; }

    /// <summary>Gets the observed SkyLuminanceFactorRgb value.</summary>
    public Vector3 SkyLuminanceFactorRgb { get; init; }

    /// <summary>Gets the observed AerialPerspectiveDistanceScale value.</summary>
    public float AerialPerspectiveDistanceScale { get; init; }

    /// <summary>Gets the observed AerialScatteringStrength value.</summary>
    public float AerialScatteringStrength { get; init; }

    /// <summary>Gets the observed AerialPerspectiveStartDepthMeters value.</summary>
    public float AerialPerspectiveStartDepthMeters { get; init; }

    /// <summary>Gets the observed HeightFogContribution value.</summary>
    public float HeightFogContribution { get; init; }

    /// <summary>Gets the observed ExposureMode value.</summary>
    public int ExposureMode { get; init; }

    /// <summary>Gets a value indicating whether native exposure is enabled.</summary>
    public bool ExposureEnabled { get; init; }

    /// <summary>Gets the observed ExposureKey value.</summary>
    public float ExposureKey { get; init; }

    /// <summary>Gets the observed ManualExposureEv value.</summary>
    public float ManualExposureEv { get; init; }

    /// <summary>Gets the observed ExposureCompensation value.</summary>
    public float ExposureCompensation { get; init; }

    /// <summary>Gets the observed ToneMapping value.</summary>
    public int ToneMapping { get; init; }

    /// <summary>Gets the observed AutoExposureMeteringMode value.</summary>
    public int AutoExposureMeteringMode { get; init; }

    /// <summary>Gets the observed AutoExposureMinEv value.</summary>
    public float AutoExposureMinEv { get; init; }

    /// <summary>Gets the observed AutoExposureMaxEv value.</summary>
    public float AutoExposureMaxEv { get; init; }

    /// <summary>Gets the observed AutoExposureSpeedUp value.</summary>
    public float AutoExposureSpeedUp { get; init; }

    /// <summary>Gets the observed AutoExposureSpeedDown value.</summary>
    public float AutoExposureSpeedDown { get; init; }

    /// <summary>Gets the observed AutoExposureLowPercentile value.</summary>
    public float AutoExposureLowPercentile { get; init; }

    /// <summary>Gets the observed AutoExposureHighPercentile value.</summary>
    public float AutoExposureHighPercentile { get; init; }

    /// <summary>Gets the observed AutoExposureMinLogLuminance value.</summary>
    public float AutoExposureMinLogLuminance { get; init; }

    /// <summary>Gets the observed AutoExposureLogLuminanceRange value.</summary>
    public float AutoExposureLogLuminanceRange { get; init; }

    /// <summary>Gets the observed AutoExposureTargetLuminance value.</summary>
    public float AutoExposureTargetLuminance { get; init; }

    /// <summary>Gets the observed AutoExposureSpotMeterRadius value.</summary>
    public float AutoExposureSpotMeterRadius { get; init; }

    /// <summary>Gets the observed dark histogram sample influence.</summary>
    public float AutoExposureBlackInfluence { get; init; }

    /// <summary>Gets the observed process-local mask resource identity; zero means no mask.</summary>
    public ulong AutoExposureMeteringMask { get; init; }

    /// <summary>Gets a value indicating whether the current exposure request awaits its mask.</summary>
    public bool ExposureMaskPending { get; init; }

    /// <summary>Gets the current mask failure, or an empty string when none is reported.</summary>
    public string ExposureMaskError { get; init; } = string.Empty;

    /// <summary>Gets the observed adaptation transition distance in EV.</summary>
    public float AutoExposureTransitionDistanceEv { get; init; }

    /// <summary>Gets the observed ordered compensation curve.</summary>
    public ImmutableArray<RuntimeExposureCompensationKey> AutoExposureCompensationCurve { get; init; } = [];

    /// <summary>Gets the observed BloomIntensity value.</summary>
    public float BloomIntensity { get; init; }

    /// <summary>Gets the observed BloomThreshold value.</summary>
    public float BloomThreshold { get; init; }

    /// <summary>Gets the observed Saturation value.</summary>
    public float Saturation { get; init; }

    /// <summary>Gets the observed Contrast value.</summary>
    public float Contrast { get; init; }

    /// <summary>Gets the observed VignetteIntensity value.</summary>
    public float VignetteIntensity { get; init; }

    /// <summary>Gets the observed DisplayGamma value.</summary>
    public float DisplayGamma { get; init; }

    /// <summary>Gets a value indicating whether fog values are present, including disabled fog.</summary>
    public bool FogExists { get; init; }

    /// <summary>Gets the native height fog and volumetric fog values.</summary>
    public FogEnvironmentData Fog { get; init; } = new();

    /// <summary>Gets a value indicating whether sky sphere values are present, including a disabled sky sphere.</summary>
    public bool SkySphereExists { get; init; }

    /// <summary>Gets the native sky sphere values; its cubemap is reported as <see cref="SkySphereCubemap"/>.</summary>
    public SkySphereEnvironmentData SkySphere { get; init; } = new();

    /// <summary>Gets the observed process-local sky sphere cubemap identity; zero means none.</summary>
    public ulong SkySphereCubemap { get; init; }

    /// <summary>Gets a value indicating whether the current environment request awaits its sky sphere cubemap.</summary>
    public bool SkySphereCubemapPending { get; init; }

    /// <summary>Gets the current sky sphere cubemap failure, or an empty string when none is reported.</summary>
    public string SkySphereCubemapError { get; init; } = string.Empty;

    /// <summary>Gets a value indicating whether sky light values are present, including a disabled sky light.</summary>
    public bool SkyLightExists { get; init; }

    /// <summary>Gets the native sky light values; its cubemap is reported as <see cref="SkyLightCubemap"/>.</summary>
    public SkyLightEnvironmentData SkyLight { get; init; } = new();

    /// <summary>Gets the observed process-local sky light cubemap identity; zero means none.</summary>
    public ulong SkyLightCubemap { get; init; }

    /// <summary>Gets a value indicating whether the current environment request awaits its sky light cubemap.</summary>
    public bool SkyLightCubemapPending { get; init; }

    /// <summary>Gets the current sky light cubemap failure, or an empty string when none is reported.</summary>
    public string SkyLightCubemapError { get; init; } = string.Empty;

    /// <summary>Gets a value indicating whether background values are present, including a disabled background.</summary>
    public bool BackgroundExists { get; init; }

    /// <summary>Gets the native display-only background values.</summary>
    public BackgroundEnvironmentData Background { get; init; } = new();
}
