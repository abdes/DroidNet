//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_SCENE_COMPOSITION_HLSLI
#define VORTEX_HDR_SCENE_COMPOSITION_HLSLI

#include "Vortex/Contracts/View/ExposureStateData.hlsli"
#include "Vortex/Contracts/View/HdrHardwareSampling.hlsli"

struct SceneCompositionConstants {
    uint status_uav; uint report_srv; uint frame_srv; uint unused0;
    uint3 unused1; uint flags;
    uint4 sky_shape;
    uint4 ap_shape;
    uint4 fog_shape;
    float ap_gain; uint arithmetic_steps; uint products; uint opaque_texels;
    uint4 unused3;
    uint4 unused4;
};

static bool SceneBoundValid(float2 value)
{
    return HdrFiniteNonnegative(value.x) && HdrFiniteNonnegative(value.y) && value.x < 1.0;
}

static float2 SceneBoundUnion(float2 a, float2 b)
{
    return asfloat(max(asuint(a) & 0x7fffffffu.xx, asuint(b) & 0x7fffffffu.xx));
}

static float2 SceneBoundSum(float2 a, float2 b)
{
    return float2(SceneBoundUnion(a, b).x, HdrUpperSum(a.y, b.y));
}

static float2 SceneBoundGain(float2 value, float gain)
{
    return (asuint(gain) & 0x7fffffffu) == 0u ? 0.0.xx : float2(value.x, HdrUpperProduct(value.y, gain));
}

static float2 SceneBoundAtMaximum(float2 value, float maximum)
{
    return float2(0.0, HdrUpperSum(HdrUpperProduct(value.x, maximum), value.y));
}

static float2 SceneTransmittanceArithmetic(float2 value)
{
    if (all((asuint(value) & 0x7fffffffu) == 0u.xx)) return value;
    // Near fading and opacity/inverse-opacity formation subtract from one.
    // Their cancellation error is absolute, not relative to a small T or RGB.
    return float2(value.x, HdrUpperSum(value.y, 16.0 / 8388608.0));
}

static float2 SceneBoundAttenuate(float2 color, float2 transmission, float maximum)
{
    const float relative = HdrUpperSum(color.x,
        HdrUpperSum(transmission.x, HdrUpperProduct(color.x, transmission.x)));
    const float absolute = HdrUpperSum(
        HdrUpperProduct(color.y, HdrUpperSum(1.0, transmission.x)),
        HdrUpperSum(HdrUpperProduct(HdrUpperProduct(maximum, transmission.y), HdrUpperSum(1.0, color.x)),
            HdrUpperProduct(color.y, transmission.y)));
    return float2(relative, absolute);
}

static float4 SceneSampledProduct(RWByteAddressBuffer status, uint product,
    uint4 shape, bool candidate, float stored_p, float inverse_stored_p)
{
    const uint index = product == 5u ? 0u : product == 6u ? 1u : 2u;
    const uint gradient_offset = 160u + index * 32u;
    const uint flags = status.Load(gradient_offset + 12u);
    const uint count = status.Load(gradient_offset + 28u);
    if ((flags & 3u) != 1u || count == 0u
        || count != shape.x * shape.y * shape.z)
        return float4(0.0, HdrBoundInfinity(), 0.0, HdrBoundInfinity());
    const float4 store = asfloat(status.Load4((candidate ? 304u : 80u) + index * 16u));
    const float3 rgb_gradient = asfloat(status.Load3(gradient_offset));
    const float3 t_gradient = asfloat(status.Load3(gradient_offset + 16u));
    const float3 stored_gradient = float3(HdrUpperProduct(rgb_gradient.x, stored_p),
        HdrUpperProduct(rgb_gradient.y, stored_p), HdrUpperProduct(rgb_gradient.z, stored_p));
    float2 rgb = HdrHardwareFilterBound(float2(store.x, HdrUpperProduct(store.y, stored_p)),
        stored_gradient, shape.xyz, candidate || shape.w != 0u);
    rgb.y = HdrUpperProduct(rgb.y, inverse_stored_p);
    const float2 alpha = HdrHardwareFilterBound(store.zw, t_gradient,
        shape.xyz, candidate || shape.w != 0u);
    return float4(rgb, alpha);
}

// Bound the additional FP32 arithmetic in both the observed and reference
// consumer chains. An identical zero-error chain preserves exact identity.
static float2 SceneArithmeticBound(float2 bound, uint steps, float inverse_p)
{
    if (all((asuint(bound) & 0x7fffffffu) == 0u.xx)) return bound;
    const float nu = HdrUpperProduct(float(steps), 1.0 / 8388608.0);
    if (!SceneBoundValid(bound) || nu >= 0.5) return float2(0.0, HdrBoundInfinity());
    const float gamma = HdrBoundUp(nu / HdrBoundDown(1.0 - nu));
    const float relative = HdrBoundUp(HdrUpperSum(bound.x,
        HdrUpperProduct(gamma, HdrUpperSum(2.0, bound.x))) / HdrBoundDown(1.0 - gamma));
    const float tiny = HdrUpperProduct(HdrUpperProduct(float(steps), asfloat(0x00800000u)), inverse_p);
    const float absolute = HdrUpperSum(HdrUpperProduct(HdrUpperSum(1.0, gamma), bound.y),
        HdrUpperProduct(HdrUpperSum(2.0, relative), tiny));
    return float2(relative, absolute);
}

static float4 SceneComposeCertificate(SceneCompositionConstants pass,
    RWByteAddressBuffer status, bool candidate, float p, float inverse_p,
    float maximum, float frame_inverse_p, out bool valid)
{
    const uint4 opaque = status.Load4(128u);
    const uint4 consumers = status.Load4(352u);
    const uint usage = consumers.z;
    valid = opaque.y == 1u && opaque.z != 0u && opaque.z == pass.opaque_texels
        && (usage & (4u | 8u | 128u)) == 0u
        && HdrFiniteNonnegative(asfloat(opaque.x)) && HdrFiniteNonnegative(maximum)
        && HdrFiniteNonnegative(pass.ap_gain);
    const float opaque_maximum = HdrUpperProduct(asfloat(opaque.x), frame_inverse_p);
    const float height_maximum = asfloat(consumers.y);
    const float translucent_maximum = asfloat(consumers.x);
    const float sky_gain = asfloat(consumers.w);
    const float3 product_maximum = asfloat(status.Load3(368u));
    float2 rgb = 0.0.xx;
    float2 coverage = 0.0.xx;
    float4 ap = 0.0.xxxx;
    float4 sky = 0.0.xxxx;
    float4 fog = 0.0.xxxx;
    if ((usage & 16u) != 0u) {
        sky = SceneSampledProduct(status, 5u, pass.sky_shape, candidate, p, inverse_p);
        sky.xy = SceneBoundAtMaximum(sky.xy, product_maximum.x);
        valid = valid && (pass.products & (1u << 4u)) != 0u
            && SceneBoundValid(sky.xy) && SceneBoundValid(sky.zw) && HdrFiniteNonnegative(sky_gain)
            && HdrFiniteNonnegative(product_maximum.x);
    }
    if ((usage & (32u | 64u)) != 0u && pass.ap_gain >= 0.0001) {
        ap = SceneSampledProduct(status, 6u, pass.ap_shape, candidate, p, inverse_p);
        ap.xy = SceneBoundGain(SceneBoundAtMaximum(ap.xy, product_maximum.y), pass.ap_gain);
        ap.zw = SceneTransmittanceArithmetic(ap.zw);
        valid = valid && (pass.products & (1u << 5u)) != 0u
            && SceneBoundValid(ap.xy) && SceneBoundValid(ap.zw) && HdrFiniteNonnegative(product_maximum.y);
    }
    if ((usage & 32u) != 0u) {
        rgb = SceneBoundSum(ap.xy, SceneBoundAttenuate(rgb, ap.zw, opaque_maximum));
        // A' = 1-(1-A)*T: preserve the shared T instead of independently
        // bounding the additive opacity and the destination attenuation.
        coverage = float2(0.0, HdrUpperSum(ap.z, ap.w));
    }
    if ((usage & 2u) != 0u && (pass.products & (1u << 9u)) != 0u) {
        fog = SceneSampledProduct(status, 10u, pass.fog_shape, candidate, p, inverse_p);
        fog.xy = SceneBoundAtMaximum(fog.xy, product_maximum.z);
        fog.zw = SceneTransmittanceArithmetic(fog.zw);
        valid = valid && SceneBoundValid(fog.xy) && SceneBoundValid(fog.zw)
            && HdrFiniteNonnegative(height_maximum) && HdrFiniteNonnegative(product_maximum.z);
        // Sky pixels are excluded by the fog/deferred-AP consumers. Height
        // scattering and the opaque/AP background share volume attenuation.
        const float background_maximum = HdrUpperSum(opaque_maximum,
            HdrUpperProduct(product_maximum.y, pass.ap_gain));
        const float attenuated_maximum = HdrUpperSum(background_maximum, height_maximum);
        rgb = SceneBoundSum(fog.xy, SceneBoundAttenuate(rgb, fog.zw, attenuated_maximum));
        coverage.y = HdrUpperSum(coverage.y, HdrUpperSum(fog.z, fog.w));
    }
    // The visible sky is a disjoint branch. A later nonnegative local-fog
    // contribution cannot amplify the absolute error already in the image.
    if ((usage & 16u) != 0u) {
        rgb = SceneBoundUnion(rgb, SceneBoundGain(sky.xy, sky_gain));
        coverage = float2(0.0, max(coverage.y, HdrUpperSum(sky.z, sky.w)));
    }
    if ((usage & 1u) != 0u) {
        valid = valid && HdrFiniteNonnegative(translucent_maximum);
        float2 source = 0.0.xx;
        if ((usage & 64u) != 0u)
            source = SceneBoundSum(ap.xy, SceneBoundAttenuate(0.0.xx, ap.zw, translucent_maximum));
        // Straight-alpha RGB blending is convex. Material alpha is common to
        // both storage modes; it does not acquire AP transmittance error.
        rgb = SceneBoundUnion(rgb, source);
    }
    rgb = SceneArithmeticBound(rgb, pass.arithmetic_steps, max(inverse_p, frame_inverse_p));
    coverage = SceneArithmeticBound(coverage, pass.arithmetic_steps, 1.0);
    valid = valid && SceneBoundValid(rgb) && SceneBoundValid(coverage);
    return valid ? float4(rgb, coverage)
        : float4(0.0, HdrBoundInfinity(), 0.0, HdrBoundInfinity());
}

static void ComposeSceneErrorBounds(uint constants_index)
{
    StructuredBuffer<SceneCompositionConstants> constants = ResourceDescriptorHeap[constants_index];
    const SceneCompositionConstants pass = constants[0];
    RWByteAddressBuffer status = ResourceDescriptorHeap[pass.status_uav];
    ByteAddressBuffer report = ResourceDescriptorHeap[pass.report_srv];
    StructuredBuffer<FrameExposureData> frames = ResourceDescriptorHeap[pass.frame_srv];
    const FrameExposureData frame = frames[0];
    const float candidate_p = asfloat(report.Load(0u));
    const float maximum = asfloat(report.Load(4u));
    bool current_valid, candidate_valid;
    const float4 current = SceneComposeCertificate(pass, status, false,
        frame.pre_exposure, frame.one_over_pre_exposure, maximum, frame.one_over_pre_exposure, current_valid);
    const float4 candidate = SceneComposeCertificate(pass, status, true,
        candidate_p, 1.0 / candidate_p, maximum, frame.one_over_pre_exposure, candidate_valid);
    const bool scale_valid = isfinite(candidate_p) && candidate_p > 0.0
        && isfinite(frame.pre_exposure) && frame.pre_exposure > 0.0
        && isfinite(frame.one_over_pre_exposure) && frame.one_over_pre_exposure > 0.0;
    status.Store4(256u, asuint(current));
    status.Store4(272u, asuint(candidate));
    status.Store4(288u, uint4(asuint(candidate_p), pass.products,
        scale_valid ? (current_valid ? 1u : 0u) | (candidate_valid ? 2u : 0u) : 0u, 0u));
}

#endif
