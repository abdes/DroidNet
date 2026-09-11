// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts native property snapshots at the Runtime boundary.</summary>
internal sealed partial class NativeRuntimeCommandTransport
{
    /// <inheritdoc/>
    public async Task<RuntimeNodeState> ObserveNodeAsync(Guid nodeId)
    {
        var state = await this.world.ObserveNodeAsync(nodeId).ConfigureAwait(false);
        return new(
            state.Exists,
            state.IsPrimarySun,
            state.Properties.Select(value => new RuntimePropertyValue(value.ComponentId, value.FieldId, value.Value)).ToImmutableArray(),
            state.GeometryKey,
            state.GeometryName,
            state.VertexCount,
            state.IndexCount,
            state.MaterialKeys.ToImmutableArray());
    }

    /// <inheritdoc/>
    public async Task<RuntimeEnvironmentState> ObserveEnvironmentAsync()
    {
        var state = await this.world.ObserveEnvironmentAsync().ConfigureAwait(false);
        return new()
        {
            Exists = state.Exists,
            AtmosphereExists = state.AtmosphereExists,
            PostProcessExists = state.PostProcessExists,
            AtmosphereEnabled = state.AtmosphereEnabled,
            SunDiskEnabled = state.SunDiskEnabled,
            PlanetRadiusMeters = state.PlanetRadiusMeters,
            AtmosphereHeightMeters = state.AtmosphereHeightMeters,
            GroundAlbedoRgb = state.GroundAlbedoRgb,
            RayleighScaleHeightMeters = state.RayleighScaleHeightMeters,
            MieScaleHeightMeters = state.MieScaleHeightMeters,
            MieAnisotropy = state.MieAnisotropy,
            SkyLuminanceFactorRgb = state.SkyLuminanceFactorRgb,
            AerialPerspectiveDistanceScale = state.AerialPerspectiveDistanceScale,
            AerialScatteringStrength = state.AerialScatteringStrength,
            AerialPerspectiveStartDepthMeters = state.AerialPerspectiveStartDepthMeters,
            HeightFogContribution = state.HeightFogContribution,
            ExposureMode = state.ExposureMode,
            ExposureEnabled = state.ExposureEnabled,
            ExposureKey = state.ExposureKey,
            ManualExposureEv = state.ManualExposureEv,
            ExposureCompensation = state.ExposureCompensation,
            ToneMapping = state.ToneMapping,
            AutoExposureMeteringMode = state.AutoExposureMeteringMode,
            AutoExposureMinEv = state.AutoExposureMinEv,
            AutoExposureMaxEv = state.AutoExposureMaxEv,
            AutoExposureSpeedUp = state.AutoExposureSpeedUp,
            AutoExposureSpeedDown = state.AutoExposureSpeedDown,
            AutoExposureLowPercentile = state.AutoExposureLowPercentile,
            AutoExposureHighPercentile = state.AutoExposureHighPercentile,
            AutoExposureMinLogLuminance = state.AutoExposureMinLogLuminance,
            AutoExposureLogLuminanceRange = state.AutoExposureLogLuminanceRange,
            AutoExposureTargetLuminance = state.AutoExposureTargetLuminance,
            AutoExposureSpotMeterRadius = state.AutoExposureSpotMeterRadius,
            BloomIntensity = state.BloomIntensity,
            BloomThreshold = state.BloomThreshold,
            Saturation = state.Saturation,
            Contrast = state.Contrast,
            VignetteIntensity = state.VignetteIntensity,
            DisplayGamma = state.DisplayGamma,
        };
    }
}
