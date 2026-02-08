//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_MEDIUM_HLSLI
#define OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_MEDIUM_HLSLI

// Common math functions (saturate, max, etc. are intrinsics, but we might need others)
#include "Common/Math.hlsli"
#include "Renderer/EnvironmentStaticData.hlsli"


//------------------------------------------------------------------------------
// Density Functions
//------------------------------------------------------------------------------

//! Computes the density of an atmospheric layer with exponential distribution.
//! Used for Rayleigh and Mie scattering.
//!
//! @param altitude_m    Height above the planet surface in meters.
//! @param scale_height_m Scale height of the layer in meters.
//! @return Normalized density (0 to 1).
float AtmosphereExponentialDensity(float altitude_m, float scale_height_m)
{
    return exp(-altitude_m / scale_height_m);
}

//! Computes ozone density using a tent distribution (Hillaire 2020).
//!
//! Defined by two linear ramps meeting at layer_width_m.
//!
//! @param altitude_m       Altitude above planet surface (meters).
//! @param layer_width_m    Altitude of peak density (meters).
//! @param linear_term_below Slope for altitude < layer_width_m.
//! @param linear_term_above Slope for altitude >= layer_width_m (typically negative).
//! @return Normalized density in [0, 1].
float OzoneAbsorptionDensity(
    float altitude_m,
    float layer_width_m,
    float linear_term_below,
    float linear_term_above)
{
    altitude_m = max(altitude_m, 0.0);

    // Below peak: linear ramp up from 0 to 1.
    // layer_width_m is expected to be > 0 (typically ~25km).
    if (altitude_m < layer_width_m)
    {
        return saturate(linear_term_below * altitude_m / layer_width_m);
    }

    // Above peak: linear ramp down from 1.
    // This requires linear_term_above to be negative (e.g., -0.666).
    return saturate(1.0 + linear_term_above * (altitude_m - layer_width_m) / layer_width_m);
}

//------------------------------------------------------------------------------
// Transmittance Functions
//------------------------------------------------------------------------------

//! Converts optical depth to transmittance using Beer-Lambert law.
//!
//! @param optical_depth (Rayleigh, Mie, Absorption) optical depths.
//! @param beta_rayleigh Rayleigh scattering coefficient (RGB).
//! @param beta_mie_ext Mie extinction coefficient (RGB).
//! @param beta_absorption Absorption coefficient (RGB).
//! @return RGB transmittance [0, 1].
float3 TransmittanceFromOpticalDepth(
    float3 optical_depth,
    float3 beta_rayleigh,
    float3 beta_mie_ext,
    float3 beta_absorption)
{
    float3 tau = beta_rayleigh * optical_depth.x
               + beta_mie_ext * optical_depth.y
               + beta_absorption * optical_depth.z;
    return exp(-tau);
}

//! Converts optical depth to transmittance using Beer-Lambert law.
//!
//! @param optical_depth (Rayleigh, Mie, Absorption) optical depths.
//! @param atmo Atmosphere parameters containing coefficients.
//! @return RGB transmittance [0, 1].
float3 TransmittanceFromOpticalDepth(
    float3 optical_depth,
    GpuSkyAtmosphereParams atmo)
{
    float d_r = optical_depth.x; // Rayleigh optical depth
    float d_m = optical_depth.y; // Mie optical depth
    float d_a = optical_depth.z; // Absorption optical depth

    float3 extinction = (atmo.rayleigh_scattering_rgb * d_r)
        + (atmo.mie_extinction_rgb * d_m)
        + (atmo.absorption_rgb * d_a);

    return exp(-extinction);
}

#endif // OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_MEDIUM_HLSLI
