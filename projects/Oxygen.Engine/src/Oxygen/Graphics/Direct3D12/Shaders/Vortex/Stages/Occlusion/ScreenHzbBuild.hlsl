//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Vortex HZB construction: a tiled downsampler that writes up to seven mips of
// the closest (max) and furthest (min) reversed-Z pyramids per dispatch.
//
// The tile dispatch builds mips 0-6, one 64 x 64 mip-0 tile per workgroup. The
// tail dispatch, a single workgroup issued after a UAV barrier, builds mips
// 7-12 from mip 6. Each thread reduces a 4 x 4 block of the dispatch's first
// level, then 2 x 2 and 1 x 1 blocks of the next two in registers; the last
// four levels reduce in group-shared memory.
//
// Pyramid extents are powers of two, so a texel inside its mip only depends on
// texels inside the previous mip, except along an axis whose previous extent
// is 1, where the 2 x 2 block clamps to its first row or column.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint HZB_BUILD_THREADS_PER_AXIS = 16u;
static const uint HZB_BUILD_MAX_MIPS = 16u;

// Mirrors HzbBuildPassConstants in HzbPyramidBuilder.cpp.
struct HzbBuildPassConstants
{
    uint source_depth_srv;
    uint source_origin_x;
    uint source_origin_y;
    uint source_width;
    uint source_height;
    uint root_width;
    uint root_height;
    uint mip_count;
    uint base_level;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint closest_mip_uavs[HZB_BUILD_MAX_MIPS];
    uint furthest_mip_uavs[HZB_BUILD_MAX_MIPS];
};

groupshared float2 gs_depths[HZB_BUILD_THREADS_PER_AXIS][HZB_BUILD_THREADS_PER_AXIS];

uint2 MipExtent(HzbBuildPassConstants c, uint level)
{
    return max(uint2(1u, 1u), uint2(c.root_width, c.root_height) >> level);
}

// Returns (closest, furthest) inputs of `level` at `coord`, clamped to the
// source view rect for mip 0 and to the previous mip otherwise.
float2 LoadInput(HzbBuildPassConstants c, uint level, uint2 coord)
{
    if (level == 0u)
    {
        // A one-slice array view of the source's slice.
        Texture2DArray<float> source
            = ResourceDescriptorHeap[c.source_depth_srv];
        const uint2 extent = uint2(c.source_width, c.source_height);
        const uint2 texel = uint2(c.source_origin_x, c.source_origin_y)
            + min(coord, extent - 1u);
        const float depth = source.Load(int4(texel, 0, 0));
        return float2(depth, depth);
    }

    const uint2 texel = min(coord, MipExtent(c, level - 1u) - 1u);
    float2 depths = float2(0.0f, 1.0f);
    const uint closest_uav = c.closest_mip_uavs[level - 1u];
    if (closest_uav != K_INVALID_BINDLESS_INDEX)
    {
        RWTexture2D<float> closest = ResourceDescriptorHeap[closest_uav];
        depths.x = closest[texel];
    }
    const uint furthest_uav = c.furthest_mip_uavs[level - 1u];
    if (furthest_uav != K_INVALID_BINDLESS_INDEX)
    {
        RWTexture2D<float> furthest = ResourceDescriptorHeap[furthest_uav];
        depths.y = furthest[texel];
    }
    return depths;
}

// Reduces the 2 x 2 block (a b / d e) of the level below `level`.
float2 Reduce(HzbBuildPassConstants c, uint level, float2 a, float2 b,
    float2 d, float2 e)
{
    if (level > 0u)
    {
        const uint2 previous = MipExtent(c, level - 1u);
        if (previous.x == 1u)
        {
            b = a;
            e = d;
        }
        if (previous.y == 1u)
        {
            d = a;
            e = b;
        }
    }
    return float2(max(max(a.x, b.x), max(d.x, e.x)),
        min(min(a.y, b.y), min(d.y, e.y)));
}

float2 ReduceInputs(HzbBuildPassConstants c, uint level, uint2 coord)
{
    const uint2 base = coord * 2u;
    return Reduce(c, level,
        LoadInput(c, level, base),
        LoadInput(c, level, base + uint2(1u, 0u)),
        LoadInput(c, level, base + uint2(0u, 1u)),
        LoadInput(c, level, base + uint2(1u, 1u)));
}

void Store(HzbBuildPassConstants c, uint level, uint2 coord, float2 depths)
{
    if (any(coord >= MipExtent(c, level)))
    {
        return;
    }
    const uint closest_uav = c.closest_mip_uavs[level];
    if (closest_uav != K_INVALID_BINDLESS_INDEX)
    {
        RWTexture2D<float> closest = ResourceDescriptorHeap[closest_uav];
        closest[coord] = depths.x;
    }
    const uint furthest_uav = c.furthest_mip_uavs[level];
    if (furthest_uav != K_INVALID_BINDLESS_INDEX)
    {
        RWTexture2D<float> furthest = ResourceDescriptorHeap[furthest_uav];
        furthest[coord] = depths.y;
    }
}

[shader("compute")]
[numthreads(HZB_BUILD_THREADS_PER_AXIS, HZB_BUILD_THREADS_PER_AXIS, 1)]
void VortexScreenHzbBuildCS(
    uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }
    StructuredBuffer<HzbBuildPassConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const HzbBuildPassConstants c = constants[0];
    const uint level0 = c.base_level;
    if (level0 >= c.mip_count)
    {
        return;
    }

    // First level: a 4 x 4 block per thread, reduced from its inputs.
    const uint2 block0 = group_id.xy * 64u + thread_id.xy * 4u;
    float2 v0[4][4];
    [unroll]
    for (uint y0 = 0u; y0 < 4u; ++y0)
    {
        [unroll]
        for (uint x0 = 0u; x0 < 4u; ++x0)
        {
            const uint2 coord = block0 + uint2(x0, y0);
            v0[y0][x0] = ReduceInputs(c, level0, coord);
            Store(c, level0, coord, v0[y0][x0]);
        }
    }
    if (level0 + 1u >= c.mip_count)
    {
        return;
    }

    // Second level: a 2 x 2 block per thread.
    const uint level1 = level0 + 1u;
    const uint2 block1 = group_id.xy * 32u + thread_id.xy * 2u;
    float2 v1[2][2];
    [unroll]
    for (uint y1 = 0u; y1 < 2u; ++y1)
    {
        [unroll]
        for (uint x1 = 0u; x1 < 2u; ++x1)
        {
            v1[y1][x1] = Reduce(c, level1, v0[2u * y1][2u * x1],
                v0[2u * y1][2u * x1 + 1u], v0[2u * y1 + 1u][2u * x1],
                v0[2u * y1 + 1u][2u * x1 + 1u]);
            Store(c, level1, block1 + uint2(x1, y1), v1[y1][x1]);
        }
    }
    if (level1 + 1u >= c.mip_count)
    {
        return;
    }

    // Third level: one texel per thread, shared with the workgroup.
    const uint level2 = level1 + 1u;
    const float2 v2 = Reduce(c, level2, v1[0][0], v1[0][1], v1[1][0], v1[1][1]);
    Store(c, level2, group_id.xy * 16u + thread_id.xy, v2);
    gs_depths[thread_id.y][thread_id.x] = v2;
    GroupMemoryBarrierWithGroupSync();

    // Last four levels: 8 x 8 down to 1 x 1 threads in group-shared memory.
    [unroll]
    for (uint step = 1u; step <= 4u; ++step)
    {
        const uint level = level2 + step;
        if (level >= c.mip_count)
        {
            return;
        }
        const uint side = HZB_BUILD_THREADS_PER_AXIS >> step;
        const bool active = all(thread_id.xy < side);
        float2 depths = float2(0.0f, 1.0f);
        if (active)
        {
            const uint2 src = thread_id.xy * 2u;
            depths = Reduce(c, level, gs_depths[src.y][src.x],
                gs_depths[src.y][src.x + 1u], gs_depths[src.y + 1u][src.x],
                gs_depths[src.y + 1u][src.x + 1u]);
        }
        GroupMemoryBarrierWithGroupSync();
        if (active)
        {
            gs_depths[thread_id.y][thread_id.x] = depths;
            Store(c, level, group_id.xy * side + thread_id.xy, depths);
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
