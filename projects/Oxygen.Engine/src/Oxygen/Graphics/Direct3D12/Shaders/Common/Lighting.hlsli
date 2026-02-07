//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Lighting Utilities
//!
//! Provides photometric unit conversions and luminance utilities for lighting:
//! - Lux (illuminance) to irradiance
//! - Lumens (luminous flux) to candelas (luminous intensity)
//! - Candelas to radiance
//! - Luminance calculations and clamping

#ifndef OXYGEN_D3D12_SHADERS_COMMON_LIGHTING_HLSLI
#define OXYGEN_D3D12_SHADERS_COMMON_LIGHTING_HLSLI

#include "Math.hlsli"

// ============================================================================
// Photometric Unit Conversions
// ============================================================================

//! Converts illuminance in lux to irradiance (W/m^2, scaled).
//!
//! In the current implementation, this is a 1:1 mapping for simplicity.
//! @param lux Illuminance in lux.
//! @return Irradiance value.
float LuxToIrradiance(float lux)
{
    return lux;
}

//! Converts luminous flux in lumens to luminous intensity in candelas.
//!
//! Candela = Lumens / Solid Angle (steradians)
//! @param lumens Luminous flux in lumens.
//! @param solid_angle_sr Solid angle in steradians.
//! @return Luminous intensity in candelas.
float LumensToCandela(float lumens, float solid_angle_sr)
{
    return lumens / max(solid_angle_sr, EPSILON_LARGE);
}

//! Converts luminous intensity to radiance using precomputed attenuation.
//!
//! @param candela Luminous intensity in candelas.
//! @param attenuation Precomputed attenuation factor (distance + spot).
//! @return Radiance value.
float CandelaToRadiance(float candela, float attenuation)
{
    return candela * attenuation;
}

// ============================================================================
// Luminance Utilities
// ============================================================================

//! Computes luminance from RGB using Rec. 709 coefficients.
//!
//! @param rgb RGB color.
//! @return Luminance value.
float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

//! Computes luminance from RGB using Rec. 601 coefficients.
//!
//! @param rgb RGB color.
//! @return Luminance value.
float LuminanceRec601(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

//! Safely clamps luminance to prevent FP16 overflow.
//!
//! @param rgb RGB color.
//! @param max_luminance Maximum allowed luminance (default: 60000.0 for FP16).
//! @return Clamped RGB color.
float3 ClampLuminance(float3 rgb, float max_luminance = 60000.0)
{
    float lum = Luminance(rgb);
    if (lum > max_luminance)
    {
        return rgb * (max_luminance / lum);
    }
    return rgb;
}

#endif // OXYGEN_D3D12_SHADERS_COMMON_LIGHTING_HLSLI
