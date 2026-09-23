//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SPOT_CONE_PRECISION_HLSLI
#define OXYGEN_VORTEX_SPOT_CONE_PRECISION_HLSLI

// Two-component expansions preserve the small cone-edge difference before
// high candela values amplify it. All arithmetic stays FP32. precise prevents
// reassociation/contraction from deleting the rounding-error terms.
static float2 SpotPrecisionSum(float a, float b)
{
    precise float high = a + b;
    precise float b_rounded = high - a;
    precise float low = (a - (high - b_rounded)) + (b - b_rounded);
    return float2(high, low);
}

static float2 SpotPrecisionProduct(float a, float b)
{
    // Split each significand into two 12-bit pieces without a large splitter
    // multiplication. The products used here have bounded normalized inputs.
    precise float a_high = asfloat(asuint(a) & 0xfffff000u);
    precise float b_high = asfloat(asuint(b) & 0xfffff000u);
    precise float a_low = a - a_high;
    precise float b_low = b - b_high;
    precise float high = a * b;
    precise float low = (((a_high * b_high - high) + a_high * b_low)
        + a_low * b_high) + a_low * b_low;
    return float2(high, low);
}

static float2 SpotPrecisionAdd(float2 a, float2 b)
{
    precise float2 sum = SpotPrecisionSum(a.x, b.x);
    precise float low = (sum.y + a.y) + b.y;
    return SpotPrecisionSum(sum.x, low);
}

static float2 SpotPrecisionMultiply(float2 a, float2 b)
{
    precise float2 product = SpotPrecisionProduct(a.x, b.x);
    precise float low = ((product.y + a.x * b.y) + a.y * b.x) + a.y * b.y;
    return SpotPrecisionSum(product.x, low);
}

static float2 SpotPrecisionDivide(float2 numerator, float2 denominator)
{
    precise float quotient = numerator.x / denominator.x;
    precise float2 remainder = SpotPrecisionAdd(numerator,
        -SpotPrecisionMultiply(denominator, float2(quotient, 0.0)));
    precise float correction = (remainder.x + remainder.y) / denominator.x;
    return SpotPrecisionSum(quotient, correction);
}

static float2 SpotPrecisionSqrt(float2 value)
{
    precise float root = sqrt(value.x);
    precise float2 remainder = SpotPrecisionAdd(value,
        -SpotPrecisionProduct(root, root));
    precise float correction = (remainder.x + remainder.y) / (2.0 * root);
    return SpotPrecisionSum(root, correction);
}

static float2 SpotPrecisionDot(float3 a, float3 b)
{
    return SpotPrecisionAdd(SpotPrecisionAdd(
        SpotPrecisionProduct(a.x, b.x), SpotPrecisionProduct(a.y, b.y)),
        SpotPrecisionProduct(a.z, b.z));
}

static float2 SpotPrecisionCrossComponent(float a, float b, float c, float d)
{
    return SpotPrecisionAdd(SpotPrecisionProduct(a, b), -SpotPrecisionProduct(c, d));
}

// Return sin(theta/2)^2 / outer_high without subtracting nearly equal cosines.
// |u x v|^2 / (2 |u||v| (|u||v| + u.v)) is the normalized half-angle coordinate.
// Scale the cross product before squaring to retain very narrow normal support.
static float2 SpotPrecisionAngularCoordinate(float3 u, float3 v,
    float2 cosine_numerator, float scale, float2 scaled_outer)
{
    precise float2 x = SpotPrecisionMultiply(
        SpotPrecisionCrossComponent(u.y, v.z, u.z, v.y), float2(scale, 0.0));
    precise float2 y = SpotPrecisionMultiply(
        SpotPrecisionCrossComponent(u.z, v.x, u.x, v.z), float2(scale, 0.0));
    precise float2 z = SpotPrecisionMultiply(
        SpotPrecisionCrossComponent(u.x, v.y, u.y, v.x), float2(scale, 0.0));
    precise float2 cross_squared = SpotPrecisionAdd(SpotPrecisionAdd(
        SpotPrecisionMultiply(x, x), SpotPrecisionMultiply(y, y)),
        SpotPrecisionMultiply(z, z));
    precise float2 norm_product = SpotPrecisionSqrt(SpotPrecisionMultiply(
        SpotPrecisionDot(u, u), SpotPrecisionDot(v, v)));
    precise float2 denominator = SpotPrecisionMultiply(
        SpotPrecisionMultiply(norm_product, SpotPrecisionAdd(norm_product, cosine_numerator)),
        SpotPrecisionMultiply(float2(2.0, 0.0), scaled_outer));
    return SpotPrecisionDivide(cross_squared, denominator);
}

static float ComputeSpotLightAngularAttenuation(float3 direction_to_source, float3 emitted_axis,
    float2 inner, float2 outer)
{
    precise float3 emitted_direction = -direction_to_source;
    precise float2 cosine_numerator = SpotPrecisionDot(emitted_direction, emitted_axis);
    // Soft hemispherical sources have zero grazing response; a hard hemisphere
    // is rejected before publication. This also avoids the antipodal singularity.
    if (cosine_numerator.x <= 0.0) return 0.0;
    // An exact power-of-two scale moves the cone support into [1,4). Scaling
    // must precede significand splitting: the low bits of a normal near-minimum
    // cone parameter would otherwise become subnormal and be flushed to zero.
    const int outer_exponent = int((asuint(outer.x) >> 23u) & 0xffu) - 127;
    const int even_exponent = outer_exponent & ~1;
    const float scale = asfloat(uint(127 - even_exponent / 2) << 23u);
    precise float outer_scaled = (outer.x * scale) * scale;
    precise float2 scaled_outer = float2(outer_scaled, 0.0);
    precise float2 coordinate = SpotPrecisionAngularCoordinate(
        emitted_direction, emitted_axis, cosine_numerator, scale, scaled_outer);
    precise float2 outer_relative = float2(1.0, outer.y);
    precise float2 edge = SpotPrecisionAdd(outer_relative, -coordinate);
    if (inner.x == outer.x && inner.y == outer.y)
        return edge.x >= 0.0 ? 1.0 : 0.0;
    precise float inner_scaled = (inner.x * scale) * scale;
    precise float2 scaled_inner = float2(inner_scaled, 0.0);
    precise float2 inner_relative = SpotPrecisionMultiply(
        SpotPrecisionDivide(scaled_inner, scaled_outer), float2(1.0, inner.y));
    precise float2 width = SpotPrecisionAdd(outer_relative, -inner_relative);
    precise float2 ramp = SpotPrecisionDivide(edge, width);
    precise float weight = saturate(ramp.x + ramp.y);
    return weight * weight;
}

#endif
