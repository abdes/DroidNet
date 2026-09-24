// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to AttachPointLight.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="LuminousFluxLumens">The LuminousFluxLumens command value.</param>
/// <param name="Range">The Range command value.</param>
/// <param name="SourceRadius">The SourceRadius command value.</param>
/// <param name="Color">The Color command value.</param>
/// <param name="AffectsWorld">The AffectsWorld command value.</param>
/// <param name="CastsShadows">The CastsShadows command value.</param>
/// <param name="ShadowBias">Shadow depth bias.</param>
/// <param name="ShadowNormalBias">Receiver normal bias in meters.</param>
/// <param name="ContactShadows">Contact-shadow participation.</param>
/// <param name="ShadowResolutionHint">Requested resolution ceiling.</param>
/// <param name="ExposureCompensation">The ExposureCompensation command value.</param>
public sealed record RuntimeAttachPointLight(
    Guid NodeId,
    float LuminousFluxLumens,
    float Range,
    float SourceRadius,
    Vector3 Color,
    bool AffectsWorld,
    bool CastsShadows,
    float ShadowBias,
    float ShadowNormalBias,
    bool ContactShadows,
    int ShadowResolutionHint,
    float ExposureCompensation) : RuntimeWorldCommand;
