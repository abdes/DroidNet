//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Common Mathematical Utilities
//!
//! Provides constants, safe math operations, and common color/luminance utilities
//! used across the rendering pipeline.

#ifndef OXYGEN_SHADERS_COMMON_MATH_HLSLI
#define OXYGEN_SHADERS_COMMON_MATH_HLSLI

// ============================================================================
// Mathematical Constants
// ============================================================================

static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;
static const float FOUR_PI = 12.56637061436;
static const float INV_PI = 0.31830988618;
static const float INV_TWO_PI = 0.15915494309;
static const float INV_FOUR_PI = 0.07957747154;

// ============================================================================
// Small Epsilons for Numerical Stability
// ============================================================================

static const float EPSILON = 1e-6;
static const float EPSILON_SMALL = 1e-8;
static const float EPSILON_LARGE = 1e-4;

// ============================================================================
// Safe Math Operations
// ============================================================================

//! Safe square root that clamps negative values to zero.
//!
//! @param x Input value.
//! @return sqrt(max(0, x)).
float SafeSqrt(float x)
{
    return sqrt(max(0.0, x));
}

//! Safe division with epsilon protection.
//!
//! @param numerator Numerator.
//! @param denominator Denominator.
//! @return numerator / max(denominator, epsilon).
float SafeDivide(float numerator, float denominator)
{
    return numerator / max(denominator, EPSILON);
}

//! Safe division with epsilon protection (float3 version).
//!
//! @param numerator Numerator.
//! @param denominator Denominator.
//! @return numerator / max(denominator, epsilon).
float3 SafeDivide(float3 numerator, float3 denominator)
{
    return numerator / max(denominator, float3(EPSILON, EPSILON, EPSILON));
}

// ============================================================================
// Clamping and Saturation
// ============================================================================

//! Clamps value to [0, 1] range.
//!
//! @param x Input value.
//! @return Clamped value.
float Saturate(float x)
{
    return saturate(x);
}

//! Clamps value to [0, 1] range (float3 version).
//!
//! @param x Input value.
//! @return Clamped value.
float3 Saturate(float3 x)
{
    return saturate(x);
}

//! Clamps value to specified range.
//!
//! @param x Input value.
//! @param min_val Minimum value.
//! @param max_val Maximum value.
//! @return Clamped value.
float Clamp(float x, float min_val, float max_val)
{
    return clamp(x, min_val, max_val);
}

//! Clamps value to specified range (float3 version).
//!
//! @param x Input value.
//! @param min_val Minimum value.
//! @param max_val Maximum value.
//! @return Clamped value.
float3 Clamp(float3 x, float3 min_val, float3 max_val)
{
    return clamp(x, min_val, max_val);
}

// ============================================================================
// Exponential and Power Utilities
// ============================================================================


//! Safe exponential that clamps the input to prevent overflow.
//!
//! @param x Input value.
//! @return exp(clamp(x, -80, 80)).
float SafeExp(float x)
{
    return exp(clamp(x, -80.0, 80.0));
}

//! Safe exponential that clamps the input to prevent overflow (float3 version).
//!
//! @param x Input value.
//! @return exp(clamp(x, -80, 80)).
float3 SafeExp(float3 x)
{
    return exp(clamp(x, float3(-80.0, -80.0, -80.0), float3(80.0, 80.0, 80.0)));
}

//! Safe power function that clamps the base to prevent negative values.
//!
//! @param base Base value.
//! @param exponent Exponent.
//! @return pow(max(0, base), exponent).
float SafePow(float base, float exponent)
{
    return pow(max(0.0, base), exponent);
}

#endif // OXYGEN_SHADERS_COMMON_MATH_HLSLI
