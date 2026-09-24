// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record DirectionalLightSource(
    LightCommonSource Common,
    float IntensityLux,
    float AngularSizeRadians,
    int AtmosphereLightSlot,
    bool UsePerPixelAtmosphereTransmittance,
    Vector3 AtmosphereDiskLuminanceScaleRgb,
    int CascadeCount,
    int SplitMode,
    float MaxShadowDistance,
    Vector4 CascadeDistances,
    float DistributionExponent,
    float TransitionFraction,
    float DistanceFadeoutFraction);
