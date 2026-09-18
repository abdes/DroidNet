//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_HARDWARE_SAMPLING_HLSLI
#define VORTEX_HDR_HARDWARE_SAMPLING_HLSLI

#include "Vortex/Contracts/View/HdrIntervalMath.hlsli"

// Mip-zero linear-clamp sampling of nonnegative 2D/3D products. All values,
// gradients and absolute errors here use the sampled texture's stored units.
// This encloses the sample; it does not replace the hardware filter.
static float2 HdrFilterArithmetic(uint dimensions, bool half_source)
{
    const float unit = half_source ? (1.0 / 1024.0) : (1.0 / 8388608.0);
    const float tiny = half_source ? (1.0 / 16777216.0) : asfloat(0x00800000u);
    // N weighted terms, d factors per term, arbitrary serial summation,
    // and conversion/complement of each weight: at most N-1+3*d roundings
    // on a term's dependency path. One ULP covers fused and conversion rules.
    const uint stages = (1u << dimensions) - 1u + 3u * dimensions;
    float relative = 0.0;
    [loop] for (uint step = 0u; step < stages; ++step)
        relative = HdrUpperSum(relative, HdrUpperProduct(HdrUpperSum(1.0, relative), unit));
    // The 3D expansion has fewer than 64 elementary operations. This term
    // covers additive underflow in values AND weights, including FP32 FTZ.
    return float2(relative, HdrUpperProduct(64.0 * tiny, HdrUpperSum(1.0, relative)));
}

static void HdrHardwareFilterCertificates(float2 texel_bound,
    float3 reference_gradient, uint3 extent, bool half_source,
    out float2 arithmetic_bound, out float2 hull_bound)
{
    arithmetic_bound = hull_bound = float2(0.0, HdrBoundInfinity());
    if (!HdrFiniteNonnegative(texel_bound.x)
        || !HdrFiniteNonnegative(texel_bound.y) || texel_bound.x >= 1.0
        || any(extent == 0u) || any(extent > 16384u)
        || !HdrFiniteNonnegative(reference_gradient.x)
        || !HdrFiniteNonnegative(reference_gradient.y)
        || !HdrFiniteNonnegative(reference_gradient.z))
        return;

    // Identical FP32 inputs, coordinates, format and sampler reproduce the
    // reference operation. This does not discard error inherited from FP16.
    if (!half_source && all((asuint(texel_bound) & 0x7fffffffu) == 0u.xx))
    {
        arithmetic_bound = hull_bound = 0.0.xx;
        return;
    }

    float displacement = 0.0;
    float diameter = 0.0;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis) {
        if (extent[axis] <= 1u) continue;
        // Two independently rounded sample addresses: 0.6/256 each, plus
        // normalized-coordinate multiply/subtract errors for both samplers.
        const float delta = HdrUpperSum(1.2 / 256.0,
            HdrUpperProduct(8.0 / 8388608.0, float(extent[axis]) + 0.5));
        displacement = HdrUpperSum(displacement,
            HdrUpperProduct(reference_gradient[axis], delta));
        diameter = HdrUpperSum(diameter, reference_gradient[axis]);
    }

    const float one_plus_r = HdrUpperSum(1.0, texel_bound.x);
    // Retain the full 3D arithmetic allowance even for singleton dimensions:
    // repeated clamp-edge taps need not be eliminated by the sampler.
    const float2 arithmetic = HdrFilterArithmetic(3u, half_source);
    const float2 reference = HdrFilterArithmetic(3u, false);
    const float observed_r = HdrUpperSum(texel_bound.x,
        HdrUpperProduct(one_plus_r, HdrUpperSum(arithmetic.x, arithmetic.y)));
    const float reference_r = HdrUpperSum(reference.x, reference.y);
    const float reference_a = HdrUpperProduct(reference.y, HdrUpperSum(diameter, 1.0));
    const float observed_a = HdrUpperSum(
        HdrUpperProduct(HdrUpperSum(1.0, arithmetic.x),
            HdrUpperSum(texel_bound.y, HdrUpperProduct(one_plus_r, displacement))),
        HdrUpperProduct(arithmetic.y, HdrUpperSum(1.0,
            HdrUpperSum(texel_bound.y,
                HdrUpperProduct(one_plus_r, HdrUpperSum(diameter, displacement))))));
    const float relative = HdrBoundUp(HdrUpperSum(observed_r, reference_r)
        / HdrBoundDown(1.0 - reference_r));
    const float absolute = HdrUpperSum(observed_a,
        HdrUpperProduct(HdrUpperSum(1.0, relative), reference_a));
    arithmetic_bound = float2(relative, absolute);

    // Independently, D3D requires a filtered value to remain in the accessed
    // texel hull. The two legal footprints span at most two edges per axis.
    // Intersecting this enclosure retains exact constant-field behavior.
    const float hull_absolute = HdrUpperSum(texel_bound.y,
        HdrUpperProduct(one_plus_r, HdrUpperSum(HdrUpperProduct(2.0, diameter),
            2.0 * asfloat(0x00800000u))));
    hull_bound = float2(texel_bound.x, hull_absolute);
}

// A whole-product affine certificate can select a dominating hull bound.
// Point queries below can additionally intersect the two distinct intervals.
static float2 HdrHardwareFilterBound(float2 texel_bound, float3 gradient,
    uint3 extent, bool half_source)
{
    float2 arithmetic, hull;
    HdrHardwareFilterCertificates(texel_bound, gradient, extent, half_source, arithmetic, hull);
    return all(hull <= arithmetic) ? hull : arithmetic;
}

static float2 HdrHardwareFilterInterval(float observed, float2 texel_bound,
    float3 reference_gradient, uint3 extent, bool half_source)
{
    if (!HdrFiniteNonnegative(observed)) return float2(0.0, HdrBoundInfinity());
    float2 arithmetic, hull;
    HdrHardwareFilterCertificates(texel_bound, reference_gradient, extent, half_source, arithmetic, hull);
    if (all((asuint(arithmetic) & 0x7fffffffu) == 0u.xx)
        && all((asuint(hull) & 0x7fffffffu) == 0u.xx)) return observed.xx;
    const float2 arithmetic_interval = HdrReferenceInterval(observed, arithmetic);
    const float2 hull_interval = HdrReferenceInterval(observed, hull);
    // Endpoints are nonnegative. Integer ordering preserves subnormal points
    // that floating min/max are permitted to flush to zero.
    return asfloat(uint2(max(asuint(arithmetic_interval.x), asuint(hull_interval.x)),
        min(asuint(arithmetic_interval.y), asuint(hull_interval.y))));
}

#endif
