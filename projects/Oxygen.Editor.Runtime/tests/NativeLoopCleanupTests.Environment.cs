// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    public Task EnvironmentObservationReadsEveryAuthoredNativeValue()
        => this.RunNativeCommandsAsync(this.CheckNativeEnvironmentAsync);

    private static RuntimeEnvironmentState ObservedEnvironmentValues() => new()
    {
        Exists = true,
        AtmosphereExists = true,
        PostProcessExists = true,
        AtmosphereEnabled = true,
        SunDiskEnabled = false,
        PlanetRadiusMeters = 6_400_000f,
        AtmosphereHeightMeters = 90_000f,
        GroundAlbedoRgb = new Vector3(0.1f, 0.2f, 0.3f),
        RayleighScaleHeightMeters = 7_000f,
        MieScaleHeightMeters = 1_400f,
        MieAnisotropy = 0.7f,
        SkyLuminanceFactorRgb = new Vector3(1.1f, 1.2f, 1.3f),
        AerialPerspectiveDistanceScale = 1.2f,
        AerialScatteringStrength = 0.8f,
        AerialPerspectiveStartDepthMeters = 40f,
        HeightFogContribution = 0.6f,
        ExposureMode = 1,
        ExposureEnabled = false,
        ExposureKey = 11f,
        ManualExposureEv = 5.5f,
        ExposureCompensation = 1.25f,
        ToneMapping = 3,
        AutoExposureMeteringMode = 2,
        AutoExposureMinEv = -3f,
        AutoExposureMaxEv = 14f,
        AutoExposureSpeedUp = 4f,
        AutoExposureSpeedDown = 2f,
        AutoExposureLowPercentile = 0.2f,
        AutoExposureHighPercentile = 0.8f,
        AutoExposureMinLogLuminance = -10f,
        AutoExposureLogLuminanceRange = 20f,
        AutoExposureTargetLuminance = 0.25f,
        AutoExposureSpotMeterRadius = 0.4f,
        BloomIntensity = 0.7f,
        BloomThreshold = 1.5f,
        Saturation = 0.9f,
        Contrast = 1.1f,
        VignetteIntensity = 0.3f,
        DisplayGamma = 2.4f,
    };

    private static RuntimeSetEnvironment EnvironmentRequest(RuntimeEnvironmentState state) => new(
        state.AtmosphereEnabled,
        state.SunDiskEnabled,
        state.PlanetRadiusMeters,
        state.AtmosphereHeightMeters,
        state.GroundAlbedoRgb,
        state.RayleighScaleHeightMeters,
        state.MieScaleHeightMeters,
        state.MieAnisotropy,
        state.SkyLuminanceFactorRgb,
        state.AerialPerspectiveDistanceScale,
        state.AerialScatteringStrength,
        state.AerialPerspectiveStartDepthMeters,
        state.HeightFogContribution,
        state.ExposureMode,
        state.ExposureEnabled,
        state.ExposureKey,
        state.ManualExposureEv,
        state.ExposureCompensation,
        state.ToneMapping,
        state.AutoExposureMeteringMode,
        state.AutoExposureMinEv,
        state.AutoExposureMaxEv,
        state.AutoExposureSpeedUp,
        state.AutoExposureSpeedDown,
        state.AutoExposureLowPercentile,
        state.AutoExposureHighPercentile,
        state.AutoExposureMinLogLuminance,
        state.AutoExposureLogLuminanceRange,
        state.AutoExposureTargetLuminance,
        state.AutoExposureSpotMeterRadius,
        state.BloomIntensity,
        state.BloomThreshold,
        state.Saturation,
        state.Contrast,
        state.VignetteIntensity,
        state.DisplayGamma);

    private async Task CheckNativeEnvironmentAsync(RuntimeCommandDispatcher commands)
    {
        var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        var activated = await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Environment observation", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = activated.Succeeded.Should().BeTrue();
        var expected = ObservedEnvironmentValues();
        var request = EnvironmentRequest(expected);
        _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, request), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();

        var observed = await commands.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().Be(expected);
        _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, request with { AtmosphereEnabled = false }), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
        observed = await commands.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().Be(expected with { AtmosphereEnabled = false });
    }
}
