//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_COMMON_PHYSICALLIGHTING_HLSLI
#define OXYGEN_D3D12_SHADERS_COMMON_PHYSICALLIGHTING_HLSLI

#ifndef kPi
#define kPi 3.14159265359
#endif

// Converts illuminance in lux to irradiance (W/m^2, scaled).
static inline float LuxToIrradiance(float lux)
{
    return lux;
}

// Converts luminous flux in lumens to luminous intensity in candelas.
static inline float LumensToCandela(float lumens, float solid_angle_sr)
{
    return lumens / max(solid_angle_sr, 1e-4);
}

// Converts luminous intensity to radiance using precomputed attenuation.
static inline float CandelaToRadiance(float candela, float attenuation)
{
    return candela * attenuation;
}

#endif
