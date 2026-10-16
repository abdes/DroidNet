// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for scene environment authoring data.
/// </summary>
/// <param name="AtmosphereEnabled">Optional atmosphere enabled flag.</param>
/// <param name="ExposureMode">Optional exposure mode.</param>
/// <param name="ManualExposureEv">Optional manual exposure in EV.</param>
/// <param name="ExposureCompensation">Optional exposure compensation in EV.</param>
/// <param name="ToneMapping">Optional tone-mapping mode.</param>
/// <param name="Background">Optional display-only background settings.</param>
/// <param name="SkyAtmosphere">Optional sky atmosphere settings.</param>
/// <param name="PostProcess">Optional post-process settings.</param>
/// <param name="Fog">Optional height fog and volumetric fog settings.</param>
/// <param name="SkySphere">Optional Sky Sphere backdrop settings.</param>
/// <param name="SkyLight">Optional Sky Light image-based lighting settings.</param>
public sealed record SceneEnvironmentEdit(
    OptionalEditValue<bool> AtmosphereEnabled,
    OptionalEditValue<ExposureMode> ExposureMode,
    OptionalEditValue<float> ManualExposureEv,
    OptionalEditValue<float> ExposureCompensation,
    OptionalEditValue<ToneMappingMode> ToneMapping,
    OptionalEditValue<BackgroundEnvironmentData> Background,
    OptionalEditValue<SkyAtmosphereEnvironmentData> SkyAtmosphere = default,
    OptionalEditValue<PostProcessEnvironmentData> PostProcess = default,
    OptionalEditValue<FogEnvironmentData> Fog = default,
    OptionalEditValue<SkySphereEnvironmentData> SkySphere = default,
    OptionalEditValue<SkyLightEnvironmentData> SkyLight = default);
