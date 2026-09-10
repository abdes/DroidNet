// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to AttachSpotLight.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="LuminousFluxLumens">The LuminousFluxLumens command value.</param>
/// <param name="Range">The Range command value.</param>
/// <param name="SourceRadius">The SourceRadius command value.</param>
/// <param name="DecayExponent">The DecayExponent command value.</param>
/// <param name="InnerConeAngleRadians">The InnerConeAngleRadians command value.</param>
/// <param name="OuterConeAngleRadians">The OuterConeAngleRadians command value.</param>
/// <param name="Color">The Color command value.</param>
/// <param name="AffectsWorld">The AffectsWorld command value.</param>
/// <param name="CastsShadows">The CastsShadows command value.</param>
/// <param name="ExposureCompensation">The ExposureCompensation command value.</param>
public sealed record RuntimeAttachSpotLight(
    Guid NodeId,
    float LuminousFluxLumens,
    float Range,
    float SourceRadius,
    float DecayExponent,
    float InnerConeAngleRadians,
    float OuterConeAngleRadians,
    Vector3 Color,
    bool AffectsWorld,
    bool CastsShadows,
    float ExposureCompensation) : RuntimeWorldCommand;
