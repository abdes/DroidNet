// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to AttachDirectionalLight.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="IntensityLux">The IntensityLux command value.</param>
/// <param name="AngularSizeRadians">The AngularSizeRadians command value.</param>
/// <param name="Color">The Color command value.</param>
/// <param name="AffectsWorld">The AffectsWorld command value.</param>
/// <param name="Mobility">The Mobility command value.</param>
/// <param name="CastsShadows">The CastsShadows command value.</param>
/// <param name="ShadowBias">The ShadowBias command value.</param>
/// <param name="ShadowNormalBias">The ShadowNormalBias command value.</param>
/// <param name="ContactShadows">The ContactShadows command value.</param>
/// <param name="ShadowResolutionHint">The ShadowResolutionHint command value.</param>
/// <param name="ExposureCompensation">The ExposureCompensation command value.</param>
/// <param name="EnvironmentContribution">The EnvironmentContribution command value.</param>
/// <param name="IsSunLight">The IsSunLight command value.</param>
/// <param name="CascadeCount">The CascadeCount command value.</param>
/// <param name="SplitMode">The SplitMode command value.</param>
/// <param name="MaxShadowDistance">The MaxShadowDistance command value.</param>
/// <param name="CascadeDistances">The CascadeDistances command value.</param>
/// <param name="DistributionExponent">The DistributionExponent command value.</param>
/// <param name="TransitionFraction">The TransitionFraction command value.</param>
/// <param name="DistanceFadeoutFraction">The DistanceFadeoutFraction command value.</param>
public sealed record RuntimeAttachDirectionalLight(
    Guid NodeId,
    float IntensityLux,
    float AngularSizeRadians,
    Vector3 Color,
    bool AffectsWorld,
    int Mobility,
    bool CastsShadows,
    float ShadowBias,
    float ShadowNormalBias,
    bool ContactShadows,
    int ShadowResolutionHint,
    float ExposureCompensation,
    bool EnvironmentContribution,
    bool IsSunLight,
    int CascadeCount,
    int SplitMode,
    float MaxShadowDistance,
    Vector4 CascadeDistances,
    float DistributionExponent,
    float TransitionFraction,
    float DistanceFadeoutFraction) : RuntimeWorldCommand;
