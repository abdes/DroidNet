//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/CubemapGeometry.hlsli"
#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/IblProductMetadata.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Shared/IblSampling.hlsli"
#include "Vortex/Shared/GroupReduction.hlsli"
#include "Vortex/Services/Environment/HeightFog.hlsli"
#include "Vortex/Services/Environment/SkyRadiance.hlsli"
#include "Vortex/Contracts/View/HdrIntervalMath.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_WorkIndex;
    uint g_Constants;
}

struct IblWork
{
    uint source_srv;
    uint output_uav;
    uint metadata_uav;
    uint partials_uav;
    uint input_uav;
    uint source_size;
    uint output_size;
    uint output_mip;
    uint maximum_mip;
    uint partial_count;
    uint revision;
    uint hemisphere_enabled;
    float4 lower_hemisphere;
    float source_rotation;
    uint capture_srv;
    uint processed_half_srv;
    uint specular_half_srv;
    uint3 group_origin;
    uint padding;
};

struct IblSkySnapshot
{
    EnvironmentStaticData environment;
    EnvironmentViewData view;
    float3 origin;
    float padding;
};

static float3 CaptureSkyRadiance(uint snapshot_srv, float3 direction)
{
    StructuredBuffer<IblSkySnapshot> snapshots = ResourceDescriptorHeap[snapshot_srv];
    IblSkySnapshot source = snapshots[0];
    // Dedicated unit-exposure sky LUT, shared fog integral, no analytic disks,
    // prior IBL, display background, local fog or volumetric fog.
    float3 atmosphere = SampleSkyViewRadiance(source.environment, source.view, direction).rgb;
    float4 fog = EvaluateSkyHeightFog(source.environment, source.view, source.origin, direction, true);
    return atmosphere * fog.a + fog.rgb;
}

static IblWork Work()
{
    StructuredBuffer<IblWork> work = ResourceDescriptorHeap[g_Constants];
    return work[g_WorkIndex];
}

groupshared float4 Shared[10][64];

[numthreads(1, 1, 1)]
void IblInitializeCS()
{
    IblWork w = Work();
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    IblProductMetadata value = (IblProductMetadata)0;
    value.source_radiance_scale = 1.0;
    value.average_brightness = 0.0;
    value.processing_flags = 0u;
    value.product_revision = w.revision;
    value.processed_half_srv = w.processed_half_srv;
    value.specular_half_srv = w.specular_half_srv;
    metadata[0] = value;
}

static void CombineShTerms(uint left, uint right)
{
    for (uint term = 0u; term < 10u; ++term) {
        Shared[term][left] += Shared[term][right];
    }
}
OXYGEN_DEFINE_GROUP_REDUCTION_64(ReduceShTerms, CombineShTerms)

// Source adapter and range reduction share the same post-hemisphere FP32 values.
static void CombineMaximumAndValidity(uint left, uint right)
{
    Shared[0][left].x = max(Shared[0][left].x, Shared[0][right].x);
    Shared[0][left].y = min(Shared[0][left].y, Shared[0][right].y);
}
OXYGEN_DEFINE_GROUP_REDUCTION_64(ReduceMaximumAndValidity, CombineMaximumAndValidity)

static void PrepareSource(uint3 id, uint3 group, uint lane, bool captured_sky)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    group += w.group_origin;
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    float maximum = 0.0;
    float valid = 1.0;
    if (all(id.xy < w.output_size)) {
        float3 cube_direction = CubemapDirectionFromFaceUv(id.z, (float2(id.xy) + 0.5) / w.output_size);
        float3 world = OxygenDirFromCubemapSamplingDir(cube_direction);
        float3 radiance;
        if (captured_sky) {
            radiance = CaptureSkyRadiance(w.capture_srv, world);
        } else {
            TextureCube<float4> source = ResourceDescriptorHeap[w.source_srv];
            float sine, cosine;
            sincos(w.source_rotation, sine, cosine);
            float3 rotated = float3(cosine * world.x - sine * world.y,
                sine * world.x + cosine * world.y, world.z);
            radiance = source.SampleLevel(linear_clamp, CubemapSamplingDirFromOxygenWS(rotated), 0.0).rgb;
        }
        if (w.hemisphere_enabled != 0u && world.z < 0.0)
            radiance = lerp(radiance, w.lower_hemisphere.rgb, w.lower_hemisphere.a);
        valid = all(isfinite(radiance)) && all(radiance >= 0.0) ? 1.0 : 0.0;
        if (valid == 0.0) radiance = 0.0.xxx;
        output[id] = float4(radiance, 1.0);
        maximum = max(radiance.r, max(radiance.g, radiance.b));
    }
    Shared[0][lane] = float4(maximum, valid, 0.0, 0.0);
    ReduceMaximumAndValidity(lane);
    if (lane == 0u) {
        uint groups = (w.output_size + 7u) / 8u;
        partials[(group.z * groups + group.y) * groups + group.x] = Shared[0][0];
    }
}

[numthreads(8, 8, 1)]
void IblPrepareCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
    uint lane : SV_GroupIndex)
{
    PrepareSource(id, group, lane, false);
}

[numthreads(8, 8, 1)]
void IblCapturePrepareCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
    uint lane : SV_GroupIndex)
{
    PrepareSource(id, group, lane, true);
}

[numthreads(64, 1, 1)]
void IblRangeCS(uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    float maximum = 0.0;
    float valid = 1.0;
    for (uint i = lane; i < w.partial_count; i += 64u) {
        maximum = max(maximum, partials[i].x);
        valid = min(valid, partials[i].y);
    }
    // Max/min preserve the exact extrema and validity bits; no summation order
    // or radiance quantization changes when distributing the reads over lanes.
    Shared[0][lane] = float4(maximum, valid, 0.0, 0.0);
    ReduceMaximumAndValidity(lane);
    if (lane != 0u) return;
    maximum = Shared[0][0].x;
    valid = Shared[0][0].y;
    IblProductMetadata value = metadata[0];
    value.source_radiance_scale = max(1.0, maximum / 65504.0);
    value.average_brightness = 0.0;
    value.processing_flags = valid > 0.0 ? kIblProductFinite : 0u;
    value.product_revision = w.revision;
    metadata[0] = value;
}

[numthreads(8, 8, 1)]
void IblNormalizeCS(uint3 id : SV_DispatchThreadID)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    if (any(id.xy >= w.output_size)) return;
    RWTexture2DArray<float4> source = ResourceDescriptorHeap[w.input_uav];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    output[id] = float4(min(source[id].rgb / metadata[0].source_radiance_scale, 65504.0), 1.0);
}

[numthreads(8, 8, 1)]
void IblMipCS(uint3 id : SV_DispatchThreadID)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    if (any(id.xy >= w.output_size)) return;
    RWTexture2DArray<float4> source = ResourceDescriptorHeap[w.input_uav];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    uint3 child = uint3(id.xy * 2u, id.z);
    output[id] = 0.25 * (source[child] + source[child + uint3(1, 0, 0)]
        + source[child + uint3(0, 1, 0)] + source[child + uint3(1, 1, 0)]);
}

[numthreads(8, 8, 1)]
void IblShCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
    uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    group += w.group_origin;
    RWTexture2DArray<float4> source = ResourceDescriptorHeap[w.input_uav];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    for (uint term = 0u; term < 10u; ++term) Shared[term][lane] = 0.0.xxxx;
    if (all(id.xy < w.source_size)) {
        float3 n = OxygenDirFromCubemapSamplingDir(CubemapDirectionFromFaceUv(id.z,
            (float2(id.xy) + 0.5) / w.source_size));
        float weight = CubemapTexelSolidAngle(id.xy, w.source_size);
        float3 weighted = source[id].rgb * weight;
        float basis[9] = { 0.282095, -0.488603 * n.y, 0.488603 * n.z,
            -0.488603 * n.x, 1.092548 * n.x * n.y, -1.092548 * n.y * n.z,
            0.315392 * (3.0 * n.z * n.z - 1.0), -1.092548 * n.x * n.z,
            0.546274 * (n.x * n.x - n.y * n.y) };
        for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
            Shared[coefficient][lane] = float4(weighted * basis[coefficient], 0.0);
        Shared[9][lane] = float4(weighted, weight);
    }
    ReduceShTerms(lane);
    if (lane == 0u) {
        uint groups = (w.source_size + 7u) / 8u;
        uint tile = (group.z * groups + group.y) * groups + group.x;
        for (uint coefficient = 0u; coefficient < 10u; ++coefficient)
            partials[tile * 10u + coefficient] = Shared[coefficient][0];
    }
}

[numthreads(64, 1, 1)]
void IblShReduceCS(uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    RWStructuredBuffer<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    for (uint term = 0u; term < 10u; ++term) Shared[term][lane] = 0.0.xxxx;
    for (uint tile = lane; tile < w.partial_count; tile += 64u)
        for (uint term = 0u; term < 10u; ++term)
            Shared[term][lane] += partials[tile * 10u + term];
    ReduceShTerms(lane);
    if (lane != 0u) return;
    float normalization = 4.0 * PI / Shared[9][0].w;
    for (uint term = 0u; term < 9u; ++term) Shared[term][0] *= normalization;
    float c0 = 1.0 / (2.0 * sqrt(PI));
    float c1 = sqrt(3.0) / (3.0 * sqrt(PI));
    float c2 = sqrt(15.0) / (8.0 * sqrt(PI));
    float c3 = sqrt(5.0) / (16.0 * sqrt(PI));
    float brightness = dot(Shared[9][0].rgb, (1.0 / 3.0).xxx) / Shared[9][0].w;
    bool valid = isfinite(brightness);
    for (uint channel = 0u; channel < 3u; ++channel) {
        float4 a = float4(-c1 * Shared[3][0][channel], -c1 * Shared[1][0][channel],
            c1 * Shared[2][0][channel], c0 * Shared[0][0][channel] - c3 * Shared[6][0][channel]);
        float4 b = float4(c2 * Shared[4][0][channel], -c2 * Shared[5][0][channel],
            3.0 * c3 * Shared[6][0][channel], -c2 * Shared[7][0][channel]);
        output[channel] = a;
        output[channel + 3u] = b;
        valid = valid && all(isfinite(a)) && all(isfinite(b));
    }
    float3 last = 0.5 * c2 * Shared[8][0].rgb;
    output[6] = float4(last, 1.0);
    output[7] = brightness.xxxx;
    metadata[0].average_brightness = brightness;
    if (!valid || !all(isfinite(last))) metadata[0].processing_flags = 0u;
}

[numthreads(8, 8, 1)]
void IblPrefilterCS(uint3 id : SV_DispatchThreadID)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    if (any(id.xy >= w.output_size)) return;
    TextureCube<float4> source = ResourceDescriptorHeap[w.source_srv];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    float3 n = CubemapDirectionFromFaceUv(id.z, (float2(id.xy) + 0.5) / w.output_size);
    float3 radiance = IblPrefilter(source, linear_clamp, n,
        IblMipToRoughness(w.output_mip, w.maximum_mip), w.source_size, w.maximum_mip);
    if (!all(isfinite(radiance)) || any(radiance < 0.0)) {
        InterlockedAnd(metadata[0].processing_flags, ~kIblProductFinite);
        radiance = 0.0.xxx;
    }
    // Normalized positive filtering is a convex combination of source texels.
    // Roundoff can exceed their normalized maximum by a few FP32 ulps.
    output[id] = float4(min(radiance, 65504.0), 1.0);
}

[numthreads(8, 8, 1)]
void IblNarrowCS(uint3 id : SV_DispatchThreadID)
{
    IblWork w = Work();
    id += w.group_origin * uint3(8u, 8u, 1u);
    if (any(id.xy >= w.output_size)) return;
    RWTexture2DArray<float4> source = ResourceDescriptorHeap[w.input_uav];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    // Narrow only the finished canonical products. These half texels never
    // feed SH, mip generation or GGX convolution.
    output[id] = HdrRoundToHalf(source[id]);
}

// Qualification reads the actual stored chains; native filtering is qualified
// separately against FP32. A rejected half certificate leaves FP32 available.
static uint PrecisionTileCount(uint source_size)
{
    uint count = 0u;
    for (uint size = source_size; size > 0u; size >>= 1u) {
        uint groups = (size + 7u) / 8u;
        count += 2u * groups * groups * 6u;
    }
    return count;
}

static void CombinePrecisionMinimum(uint left, uint right)
{
    Shared[0][left] = min(Shared[0][left], Shared[0][right]);
}
OXYGEN_DEFINE_GROUP_REDUCTION_64(ReducePrecisionMinimum, CombinePrecisionMinimum)

static float HalfTexelGain(float3 reference_min, float3 reference_max,
    float3 half_min, float3 half_max, float source_scale);

[numthreads(8, 8, 1)]
void IblPrecisionRangeCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    uint tile = group.y * 65535u + group.x;
    if (tile >= PrecisionTileCount(w.source_size)) return;
    // The half-work records alternate processed/specular at each mip. Every
    // group reads one record and writes one disjoint gain/validity partial.
    uint local_tile = tile;
    uint record = g_WorkIndex;
    uint groups = (w.output_size + 7u) / 8u;
    StructuredBuffer<IblWork> work = ResourceDescriptorHeap[g_Constants];
    while (local_tile >= groups * groups * 6u) {
        local_tile -= groups * groups * 6u;
        w = work[++record];
        groups = (w.output_size + 7u) / 8u;
    }
    uint face = local_tile / (groups * groups);
    uint2 tile_xy = uint2(local_tile % groups, (local_tile / groups) % groups);
    uint3 id = uint3(tile_xy * 8u + uint2(lane % 8u, lane / 8u), face);
    RWTexture2DArray<float4> canonical = ResourceDescriptorHeap[NonUniformResourceIndex(w.input_uav)];
    RWTexture2DArray<float4> half_product = ResourceDescriptorHeap[NonUniformResourceIndex(w.output_uav)];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    Shared[0][lane] = float4(asfloat(0x7f7fffffu), 1.0, 0.0, 0.0);
    if (all(id.xy < w.output_size)) {
        float4 reference = canonical[id];
        float4 narrowed = half_product[id];
        bool valid = HdrFiniteNonnegative(reference.r) && HdrFiniteNonnegative(reference.g)
            && HdrFiniteNonnegative(reference.b) && reference.a == 1.0
            && HdrFiniteNonnegative(narrowed.r) && HdrFiniteNonnegative(narrowed.g)
            && HdrFiniteNonnegative(narrowed.b) && narrowed.a == 1.0;
        if (!valid) reference = narrowed = 0.0.xxxx;
        float3 reference_low, reference_high, half_low, half_high;
        [unroll] for (uint channel = 0u; channel < 3u; ++channel) {
            // Texture filtering may flush FP32 subnormals. Enclose both the
            // stored value and zero, widening maxima before any arithmetic.
            reference_low[channel] = (asuint(reference[channel]) & 0x7fffffffu) < 0x00800000u
                ? 0.0 : reference[channel];
            half_low[channel] = (asuint(narrowed[channel]) & 0x7fffffffu) < 0x00800000u
                ? 0.0 : narrowed[channel];
            reference_high[channel] = HdrUpperOperand(reference[channel]);
            half_high[channel] = HdrUpperOperand(narrowed[channel]);
        }
        float gain = valid ? HalfTexelGain(reference_low, reference_high,
            half_low, half_high, metadata[0].source_radiance_scale) : 0.0;
        Shared[0][lane] = float4(gain, valid ? 1.0 : 0.0, 0.0, 0.0);
    }
    ReducePrecisionMinimum(lane);
    if (lane == 0u) partials[tile] = Shared[0][0];
}

[numthreads(64, 1, 1)]
void IblPrecisionReduceCS(uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    Shared[0][lane] = float4(asfloat(0x7f7fffffu), 1.0, 0.0, 0.0);
    uint tile_count = PrecisionTileCount(w.source_size);
    for (uint tile = lane; tile < tile_count; tile += 64u) {
        Shared[0][lane] = min(Shared[0][lane], partials[tile]);
    }
    ReducePrecisionMinimum(lane);
    if (lane != 0u) return;
    // All lanes have finished reading. Reuse the final certificate location even
    // when it overlaps the now-dead precision partials.
    uint offset = w.partial_count * 10u;
    partials[offset] = Shared[0][0];
}

static float HalfTexelGain(float3 reference_min, float3 reference_max,
    float3 half_min, float3 half_max, float source_scale)
{
    // Gain excludes source scale. The frozen display envelope is 2^32.
    const float absolute_budget = HdrBoundDown(1.0e-5 / 4294967296.0);
    float gain = asfloat(0x7f7fffffu);
    bool ev_safe = true;
    for (uint channel = 0u; channel < 3u; ++channel) {
        float error = max(HdrUpperDifference(reference_max[channel], half_min[channel]),
            HdrUpperDifference(half_max[channel], reference_min[channel]));
        float relative_allowance = HdrBoundDown(reference_min[channel] * HdrBoundDown(0.0025));
        float excess = HdrUpperDifference(error, relative_allowance);
        if (excess > 0.0)
            gain = min(gain, HdrBoundDown(absolute_budget / HdrUpperProduct(excess, source_scale)));
        // These constants lie strictly inside 2^(+/-1/1024). Independent
        // per-channel enclosures remain valid under any nonnegative tint.
        ev_safe = ev_safe
            && half_min[channel] >= HdrBoundUp(reference_max[channel] * 0.9993234)
            && half_max[channel] <= HdrBoundDown(reference_min[channel] * 1.0006771);
    }
    if (!ev_safe) {
        float maximum = max(reference_max.r, max(reference_max.g, reference_max.b));
        if (maximum > 0.0)
            gain = min(gain, HdrBoundDown(absolute_budget / HdrUpperProduct(maximum, source_scale)));
    }
    return gain;
}

[numthreads(1, 1, 1)]
void IblCompleteCS()
{
    IblWork w = Work();
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    uint offset = w.partial_count * 10u;
    float2 certificate = partials[offset].xy;
    if (certificate.y == 1.0 && HdrFiniteNonnegative(certificate.x)) {
        metadata[0].maximum_half_gain = certificate.x;
        metadata[0].precision_flags = kIblHalfCertificateComplete;
    }
    metadata[0].processing_flags |= kIblProductComplete;
}
