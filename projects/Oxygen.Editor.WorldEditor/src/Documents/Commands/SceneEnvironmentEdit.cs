// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for scene environment authoring data.
/// </summary>
/// <param name="AtmosphereEnabled">Optional atmosphere enabled flag.</param>
/// <param name="SunNodeId">Optional sun node identity.</param>
/// <param name="ExposureMode">Optional exposure mode.</param>
/// <param name="ManualExposureEv">Optional manual exposure in EV.</param>
/// <param name="ExposureCompensation">Optional exposure compensation in EV.</param>
/// <param name="ToneMapping">Optional tone-mapping mode.</param>
/// <param name="BackgroundColor">Optional linear background color.</param>
/// <param name="SkyAtmosphere">Optional sky atmosphere settings.</param>
/// <param name="PostProcess">Optional post-process settings.</param>
public sealed record SceneEnvironmentEdit(
    OptionalEditValue<bool> AtmosphereEnabled,
    OptionalEditValue<Guid?> SunNodeId,
    OptionalEditValue<ExposureMode> ExposureMode,
    OptionalEditValue<float> ManualExposureEv,
    OptionalEditValue<float> ExposureCompensation,
    OptionalEditValue<ToneMappingMode> ToneMapping,
    OptionalEditValue<Vector3> BackgroundColor,
    OptionalEditValue<SkyAtmosphereEnvironmentData> SkyAtmosphere = default,
    OptionalEditValue<PostProcessEnvironmentData> PostProcess = default);
