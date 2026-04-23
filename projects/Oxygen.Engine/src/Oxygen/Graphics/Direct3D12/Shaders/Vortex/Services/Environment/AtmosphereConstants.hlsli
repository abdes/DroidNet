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
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"

// =============================================================================
// UE5 Reference Constants (from RenderSkyRayMarching.hlsl)
// =============================================================================

//! Fixed offset within each raymarch segment (UE5: SampleSegmentT).
//! Value of 0.3 provides good convergence with minimal samples.
static const float kSegmentSampleOffset = 0.3;

//! Aerial perspective slice count (UE5: AP_SLICE_COUNT).
static const uint kAerialPerspectiveSliceCount = 32;

//! Float accuracy offset in atmosphere-space km (UE5: PLANET_RADIUS_OFFSET).
static const float PLANET_RADIUS_OFFSET = 0.001f;

//! Kilometers per aerial perspective slice (UE5: AP_KM_PER_SLICE).
static const float AP_KM_PER_SLICE = 4.0f;
static const float AP_KM_PER_SLICE_INV = 1.0f / AP_KM_PER_SLICE;

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

//! Sun disk edge softness for anti-aliasing in ANGLE space (radians).
//! Keep this small; larger values turn the solar disk into a visible blob.
static const float kSunDiskEdgeSoftness = 0.0005;
//! UE5.7 parity sampler for atmosphere LUTs.
//! Do not replace with the default wrap sampler: transmittance and camera
//! aerial LUTs are non-periodic, and wrap creates the below-horizon elliptical
//! bands / bright artifacts that showed up around sunset.
static const uint kAtmosphereLinearClampSampler = VORTEX_SAMPLER_LINEAR_CLAMP;

// =============================================================================
// Physical Defaults (Artist Overridable via GpuSkyAtmosphereParams)
// =============================================================================

// =============================================================================
// Physical Defaults (Artist Overridable via GpuSkyAtmosphereParams)
// =============================================================================

//! Default ozone layer width scaling factor.
//! NOTE: This should be an artist parameter.
static const float kDefaultOzoneWidthScale = 0.6;

#endif // OXYGEN_GRAPHICS_SHADERS_ATMOSPHERE_CONSTANTS_HLSLI
