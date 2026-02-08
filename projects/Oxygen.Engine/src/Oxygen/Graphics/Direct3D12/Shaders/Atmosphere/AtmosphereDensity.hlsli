//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_ATMOSPHERE_DENSITY_HLSLI
#define OXYGEN_ATMOSPHERE_DENSITY_HLSLI

struct AtmosphereDensityLayer
{
    float width_m;
    float exp_term;
    float linear_term;
    float constant_term;
};

struct AtmosphereDensityProfile
{
    AtmosphereDensityLayer layers[2];
};

#endif // OXYGEN_ATMOSPHERE_DENSITY_HLSLI
