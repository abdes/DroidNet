//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Terminology and Units (Physical conventions used in this system):
// - Radiance: W/(sr·m²) or Nits (cd/m²). The unit of inscattered light in LUTs.
// - Illuminance: Lux (lm/m²). The unit of sun light (color * intensity).
// - Luminance: cd/m². Used for perceived brightness (often Nits).

#ifndef OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_CONSTANTS_HLSLI
#define OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_CONSTANTS_HLSLI

#include "Common/Math.hlsli"

// =============================================================================
// UE5 Reference Constants (from RenderSkyRayMarching.hlsl)
// =============================================================================

//! Fixed offset within each raymarch segment (UE5: SampleSegmentT).
//! Value of 0.3 provides good convergence with minimal samples.
static const float kSegmentSampleOffset = 0.3;

//! Aerial perspective slice count (UE5: AP_SLICE_COUNT).
static const uint kAerialPerspectiveSliceCount = 32;

//! Kilometers per aerial perspective slice (UE5: AP_KM_PER_SLICE).
static const float kAerialPerspectiveKmPerSlice = 4.0;

// =============================================================================
// Safety Limits
// =============================================================================

//! Maximum value for FP16 textures (~65504, use 65000 for safety margin).
static const float kFP16SafeMax = 65000.0;

//! Maximum sun radiance to prevent FP16 overflow in sky capture.
static const float kSunRadianceSafeMax = 64000.0;

//! "Infinite" optical depth for sun blocked by planet.
static const float kInfiniteOpticalDepth = 1e6;

//! Small value to avoid division by zero or other numerical issues.
//! Semantic alias for EPSILON in atmosphere context.
static const float kAtmosphereEpsilon = EPSILON;

// =============================================================================
// Sky-View LUT Parameters
// =============================================================================

//! Zenith filter threshold in sine of zenith angle (~2.9 degrees).
//! Below this angle, azimuth averaging is applied to reduce artifacts.
static const float kZenithFilterThreshold = 0.05;

//! Sun disk edge softness for anti-aliasing (radians).
static const float kSunDiskEdgeSoftness = 0.002;

// =============================================================================
// Physical Defaults (Artist Overridable via GpuSkyAtmosphereParams)
// =============================================================================

//! Default Mie single-scattering albedo for Earth-like atmosphere.
//! NOTE: This should be an artist parameter.
static const float kDefaultMieSingleScatteringAlbedo = 0.9;

//! Default ozone layer width scaling factor.
//! NOTE: This should be an artist parameter.
static const float kDefaultOzoneWidthScale = 0.6;

#endif // OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_CONSTANTS_HLSLI
