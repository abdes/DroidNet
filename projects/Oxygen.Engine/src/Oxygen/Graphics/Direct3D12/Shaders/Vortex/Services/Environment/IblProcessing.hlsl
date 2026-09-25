//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/IblProductMetadata.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Shared/IblSampling.hlsli"

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
    float3 padding;
};

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
    IblProductMetadata value;
    value.source_radiance_scale = 1.0;
    value.average_brightness = 0.0;
    value.processing_flags = 0u;
    value.product_revision = w.revision;
    metadata[0] = value;
}

static void ReduceSum(uint lane, uint terms)
{
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 32u; offset > 0u; offset >>= 1u) {
        if (lane < offset) {
            for (uint term = 0u; term < terms; ++term)
                Shared[term][lane] += Shared[term][lane + offset];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

// Source adapter and range reduction share the same post-hemisphere FP32 values.
[numthreads(8, 8, 1)]
void IblPrepareCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
    uint lane : SV_GroupIndex)
{
    IblWork w = Work();
    TextureCube<float4> source = ResourceDescriptorHeap[w.source_srv];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    float maximum = 0.0;
    float valid = 1.0;
    if (all(id.xy < w.output_size)) {
        float3 cube_direction = IblCubeDirection(id.z, (float2(id.xy) + 0.5) / w.output_size);
        float3 world = OxygenDirFromCubemapSamplingDir(cube_direction);
        float sine, cosine;
        sincos(w.source_rotation, sine, cosine);
        float3 rotated = float3(cosine * world.x - sine * world.y,
            sine * world.x + cosine * world.y, world.z);
        float3 radiance = source.SampleLevel(linear_clamp, CubemapSamplingDirFromOxygenWS(rotated), 0.0).rgb;
        if (w.hemisphere_enabled != 0u && world.z < 0.0)
            radiance = lerp(radiance, w.lower_hemisphere.rgb, w.lower_hemisphere.a);
        valid = all(isfinite(radiance)) && all(radiance >= 0.0) ? 1.0 : 0.0;
        if (valid == 0.0) radiance = 0.0.xxx;
        output[id] = float4(radiance, 1.0);
        maximum = max(radiance.r, max(radiance.g, radiance.b));
    }
    Shared[0][lane] = float4(maximum, valid, 0.0, 0.0);
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 32u; offset > 0u; offset >>= 1u) {
        if (lane < offset) {
            Shared[0][lane].x = max(Shared[0][lane].x, Shared[0][lane + offset].x);
            Shared[0][lane].y = min(Shared[0][lane].y, Shared[0][lane + offset].y);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0u) {
        uint groups = (w.output_size + 7u) / 8u;
        partials[(group.z * groups + group.y) * groups + group.x] = Shared[0][0];
    }
}

[numthreads(1, 1, 1)]
void IblRangeCS()
{
    IblWork w = Work();
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    float maximum = 0.0;
    float valid = 1.0;
    for (uint i = 0u; i < w.partial_count; ++i) {
        maximum = max(maximum, partials[i].x);
        valid = min(valid, partials[i].y);
    }
    IblProductMetadata value;
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
    RWTexture2DArray<float4> source = ResourceDescriptorHeap[w.input_uav];
    RWStructuredBuffer<float4> partials = ResourceDescriptorHeap[w.partials_uav];
    for (uint term = 0u; term < 10u; ++term) Shared[term][lane] = 0.0.xxxx;
    if (all(id.xy < w.source_size)) {
        float3 n = OxygenDirFromCubemapSamplingDir(IblCubeDirection(id.z,
            (float2(id.xy) + 0.5) / w.source_size));
        float weight = IblTexelSolidAngle(id.xy, w.source_size);
        float3 weighted = source[id].rgb * weight;
        float basis[9] = { 0.282095, -0.488603 * n.y, 0.488603 * n.z,
            -0.488603 * n.x, 1.092548 * n.x * n.y, -1.092548 * n.y * n.z,
            0.315392 * (3.0 * n.z * n.z - 1.0), -1.092548 * n.x * n.z,
            0.546274 * (n.x * n.x - n.y * n.y) };
        for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
            Shared[coefficient][lane] = float4(weighted * basis[coefficient], 0.0);
        Shared[9][lane] = float4(weighted, weight);
    }
    ReduceSum(lane, 10u);
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
    ReduceSum(lane, 10u);
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
    if (any(id.xy >= w.output_size)) return;
    TextureCube<float4> source = ResourceDescriptorHeap[w.source_srv];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[w.output_uav];
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    float3 n = IblCubeDirection(id.z, (float2(id.xy) + 0.5) / w.output_size);
    float3 radiance = IblPrefilter(source, linear_clamp, n,
        IblMipToRoughness(w.output_mip, w.maximum_mip), w.source_size, w.maximum_mip);
    if (!all(isfinite(radiance)) || any(radiance < 0.0)) {
        InterlockedAnd(metadata[0].processing_flags, ~kIblProductFinite);
        radiance = 0.0.xxx;
    }
    // Positive normalized filtering is a convex combination of FP16 source
    // texels. Roundoff can exceed their maximum by a few FP32 ulps.
    output[id] = float4(min(radiance, 65504.0), 1.0);
}

[numthreads(1, 1, 1)]
void IblCompleteCS()
{
    IblWork w = Work();
    RWStructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[w.metadata_uav];
    metadata[0].processing_flags |= kIblProductComplete;
}
