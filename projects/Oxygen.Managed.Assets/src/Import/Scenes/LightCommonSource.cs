// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record LightCommonSource(
    bool AffectsWorld,
    float Red,
    float Green,
    float Blue,
    bool CastsShadows,
    float ExposureCompensation);
