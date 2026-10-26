// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Interop.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts native property snapshots at the Runtime boundary.</summary>
internal sealed partial class NativeRuntimeCommandTransport
{
    /// <inheritdoc/>
    public async Task<RuntimeNodeState> ObserveNodeAsync(NodeId nodeId)
    {
        var state = await this.world.ObserveNodeAsync(nodeId.Value).ConfigureAwait(false);
        return new(
            state.Exists,
            state.IsPrimarySun,
            [.. state.Properties.Select(RuntimeTransportConversion.FromNative)],
            state.GeometryKey,
            state.GeometryName,
            state.VertexCount,
            state.IndexCount,
            [.. state.MaterialKeys])
        { MaterialBaseColors = [.. state.MaterialBaseColors] };
    }

    /// <inheritdoc/>
    public async Task<RuntimeSceneHierarchy> ReadSceneHierarchyAsync(string virtualPath)
    {
        var hierarchy = await this.world.ReadSceneHierarchyAsync(virtualPath).ConfigureAwait(false);
        return new(
            hierarchy.Exists,
            hierarchy.SceneKey,
            [.. hierarchy.Nodes.Select(static node => new RuntimeSceneHierarchyNode(
                NodePath.Of(node.Path.Select(NodeId.FromValue)),
                node.Name,
                node.Parent < 0 ? null : node.Parent,
                node.SceneKey,
                node.InstanceKey == Guid.Empty ? null : node.InstanceKey))],
            [.. hierarchy.Issues.Select(static issue => new RuntimeSceneHierarchyIssue(
                (RuntimeSceneHierarchyIssueCode)issue.Code,
                issue.Node < 0 ? null : issue.Node,
                issue.SceneKey))]);
    }

    /// <inheritdoc/>
    public async Task<RuntimeSceneNodeValues> ReadSceneNodeAsync(string virtualPath, NodePath path)
    {
        var values = await this.world.ReadSceneNodeAsync(virtualPath, RuntimeTransportConversion.ToNative(path)).ConfigureAwait(false);
        return new(
            values.Exists,
            values.Name ?? string.Empty,
            values.GeometryPath,
            [.. values.Properties.Select(RuntimeTransportConversion.FromNative).OfType<RuntimeSceneNodeProperty>()]);
    }

    /// <inheritdoc/>
    public async Task<RuntimeEnvironmentState> ObserveEnvironmentAsync()
    {
        var state = await this.world.ObserveEnvironmentAsync().ConfigureAwait(false);
        return WithEnvironmentParts(WithExposure(AtmosphereFromNative(state), state), state);
    }

    private static RuntimeEnvironmentState AtmosphereFromNative(EnvironmentStateManaged state)
        => new()
        {
            Exists = state.Exists,
            AtmosphereExists = state.AtmosphereExists,
            PostProcessExists = state.PostProcessExists,
            SkyLightObserved = state.SkyLightObserved,
            SkyLightEnabled = state.SkyLightEnabled,
            SkyLightUsable = state.SkyLightUsable,
            SkyLightUnavailableReason = (RuntimeSkyLightUnavailableReason)state.SkyLightUnavailableReason,
            SkyLightEmptyCapture = state.SkyLightEmptyCapture,
            SkyLightSceneLifetime = state.SkyLightSceneLifetime,
            SkyLightFrameSequence = state.SkyLightFrameSequence,
            SkyLightPublishedRevision = state.SkyLightPublishedRevision,
            SkyLightPublishedSourceRevision = state.SkyLightPublishedSourceRevision,
            SkyLightDesiredSourceRevision = state.SkyLightDesiredSourceRevision,
            SkyLightSourceAgeFrames = state.SkyLightSourceAgeFrames,
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
        };

    private static RuntimeEnvironmentState WithExposure(RuntimeEnvironmentState observed, EnvironmentStateManaged state)
        => observed with
        {
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
            AutoExposureBlackInfluence = state.AutoExposureBlackInfluence,
            AutoExposureMeteringMask = state.AutoExposureMeteringMask,
            ExposureMaskPending = state.ExposureMaskPending,
            ExposureMaskError = state.ExposureMaskError ?? string.Empty,
            AutoExposureTransitionDistanceEv = state.AutoExposureTransitionDistanceEv,
            AutoExposureCompensationCurve = [.. state.AutoExposureCompensationCurve.Select(static key => new RuntimeExposureCompensationKey(key.MeteredEv, key.CompensationEv))],
            BloomIntensity = state.BloomIntensity,
            BloomThreshold = state.BloomThreshold,
            Saturation = state.Saturation,
            Contrast = state.Contrast,
            VignetteIntensity = state.VignetteIntensity,
            DisplayGamma = state.DisplayGamma,
        };

    private static RuntimeEnvironmentState WithEnvironmentParts(RuntimeEnvironmentState observed, EnvironmentStateManaged state)
        => observed with
        {
            FogExists = state.FogExists,
            Fog = RuntimeTransportConversion.FromNative(state.Fog),
            SkySphereExists = state.SkySphereExists,
            SkySphere = RuntimeTransportConversion.FromNative(state.SkySphere),
            SkySphereCubemap = state.SkySphereCubemap,
            SkySphereCubemapPending = state.SkySphereCubemapPending,
            SkySphereCubemapError = state.SkySphereCubemapError ?? string.Empty,
            SkyLightExists = state.SkyLightExists,
            SkyLight = RuntimeTransportConversion.FromNative(state.SkyLight),
            SkyLightCubemap = state.SkyLightCubemap,
            SkyLightCubemapPending = state.SkyLightCubemapPending,
            SkyLightCubemapError = state.SkyLightCubemapError ?? string.Empty,
            BackgroundExists = state.BackgroundExists,
            Background = RuntimeTransportConversion.FromNative(state.Background),
        };
}
