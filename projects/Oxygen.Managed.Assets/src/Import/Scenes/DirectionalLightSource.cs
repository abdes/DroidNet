// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record DirectionalLightSource(
    LightCommonSource Common,
    float IntensityLux,
    float AngularSizeRadians,
    bool EnvironmentContribution,
    bool IsSunLight);
