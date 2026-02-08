//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Atmospheric Scattering Phase Functions
//!
//! Provides canonical implementations of phase functions for Rayleigh and Mie
//! scattering. These functions describe the angular distribution of scattered
//! light and are fundamental to physically-based atmosphere rendering.

#ifndef OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_PHASE_HLSLI
#define OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_PHASE_HLSLI

#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"

//------------------------------------------------------------------------------
// Phase Functions
//------------------------------------------------------------------------------

//! Rayleigh phase function for molecular scattering.
//!
//! Describes scattering by particles much smaller than the wavelength of light
//! (e.g., air molecules). Produces symmetric forward/backward scattering with
//! a characteristic dipole pattern.
//!
//! @param cos_theta Cosine of angle between incident and scattered directions.
//! @return Phase function value (sr^-1), normalized so integral over sphere = 1.
float RayleighPhase(float cos_theta)
{
    // Normalized Rayleigh phase: (3 / 16π) * (1 + cos²θ)
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

//! Henyey-Greenstein phase function for aerosol scattering.
//!
//! Empirical phase function commonly used for Mie scattering. Provides a
//! simple parameterization of forward/backward scattering via the asymmetry
//! parameter g.
//!
//! @param cos_theta Cosine of scattering angle.
//! @param g Asymmetry parameter [-1, 1]. Positive = forward scattering,
//!          negative = backward scattering, 0 = isotropic.
//! @return Phase function value (sr^-1).
float HenyeyGreensteinPhase(float cos_theta, float g)
{
    // Clamp g to avoid singularities
    g = clamp(g, -0.99, 0.99);
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-5);

    // HG phase: (1 - g²) / (4π * (1 + g² - 2g*cosθ)^1.5)
    return (1.0 - g2) * INV_FOUR_PI / pow(denom, 1.5);
}

//! Cornette-Shanks phase function (UE5 reference).
//!
//! More physically accurate than standard Henyey-Greenstein for Mie scattering.
//! Includes a cos²θ term that better matches real aerosol scattering patterns.
//! Used in Unreal Engine 5's atmosphere system.
//!
//! @param cos_theta Cosine of scattering angle.
//! @param g Asymmetry parameter [-1, 1].
//! @return Phase function value (sr^-1), clamped for FP16 safety.
float CornetteShanksMiePhase(float cos_theta, float g)
{
    // Normalization factor: k = (3 / 8π) * (1 - g²) / (2 + g²)
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    float denom = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-5);

    // Cornette-Shanks: k * (1 + cos²θ) / (1 + g² - 2g*cosθ)^1.5
    float result = k * (1.0 + cos_theta * cos_theta) / pow(denom, 1.5);

    // Clamp to prevent FP16 overflow in LUT storage
    return min(result, kFP16SafeMax);
}

#endif // OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_PHASE_HLSLI
