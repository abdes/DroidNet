// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record SpotLightSource(
    LightCommonSource Common,
    float LuminousFluxLumens,
    float Range,
    float SourceRadius,
    float DecayExponent,
    float InnerConeAngleRadians,
    float OuterConeAngleRadians);
