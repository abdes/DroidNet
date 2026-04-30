// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for a directional light component.
/// </summary>
/// <param name="Color">Optional linear RGB color multiplier.</param>
/// <param name="IntensityLux">Optional illuminance in lux.</param>
/// <param name="IsSunLight">Optional primary-sun candidate flag.</param>
/// <param name="EnvironmentContribution">Optional atmosphere/environment contribution flag.</param>
/// <param name="CastsShadows">Optional shadow-casting flag.</param>
/// <param name="AffectsWorld">Optional world-lighting flag.</param>
/// <param name="AngularSizeRadians">Optional angular size in radians.</param>
/// <param name="ExposureCompensation">Optional light exposure compensation in EV.</param>
/// <param name="Mobility">Optional authoring mobility.</param>
/// <param name="ShadowBias">Optional shadow depth bias.</param>
/// <param name="ShadowNormalBias">Optional shadow normal bias.</param>
/// <param name="ContactShadows">Optional contact shadow flag.</param>
/// <param name="ShadowResolutionHint">Optional shadow-map resolution hint.</param>
/// <param name="CascadeCount">Optional directional cascade count.</param>
/// <param name="SplitMode">Optional cascade split mode.</param>
/// <param name="MaxShadowDistance">Optional maximum shadow distance.</param>
/// <param name="CascadeDistance0">Optional first manual cascade distance.</param>
/// <param name="CascadeDistance1">Optional second manual cascade distance.</param>
/// <param name="CascadeDistance2">Optional third manual cascade distance.</param>
/// <param name="CascadeDistance3">Optional fourth manual cascade distance.</param>
/// <param name="DistributionExponent">Optional generated split distribution exponent.</param>
/// <param name="TransitionFraction">Optional cascade transition fraction.</param>
/// <param name="DistanceFadeoutFraction">Optional distance fadeout fraction.</param>
public sealed record DirectionalLightEdit(
    OptionalEditValue<Vector3> Color,
    OptionalEditValue<float> IntensityLux,
    OptionalEditValue<bool> IsSunLight,
    OptionalEditValue<bool> EnvironmentContribution,
    OptionalEditValue<bool> CastsShadows,
    OptionalEditValue<bool> AffectsWorld,
    OptionalEditValue<float> AngularSizeRadians,
    OptionalEditValue<float> ExposureCompensation,
    OptionalEditValue<LightMobility> Mobility = default,
    OptionalEditValue<float> ShadowBias = default,
    OptionalEditValue<float> ShadowNormalBias = default,
    OptionalEditValue<bool> ContactShadows = default,
    OptionalEditValue<ShadowResolutionHint> ShadowResolutionHint = default,
    OptionalEditValue<int> CascadeCount = default,
    OptionalEditValue<DirectionalCsmSplitMode> SplitMode = default,
    OptionalEditValue<float> MaxShadowDistance = default,
    OptionalEditValue<float> CascadeDistance0 = default,
    OptionalEditValue<float> CascadeDistance1 = default,
    OptionalEditValue<float> CascadeDistance2 = default,
    OptionalEditValue<float> CascadeDistance3 = default,
    OptionalEditValue<float> DistributionExponent = default,
    OptionalEditValue<float> TransitionFraction = default,
    OptionalEditValue<float> DistanceFadeoutFraction = default);
