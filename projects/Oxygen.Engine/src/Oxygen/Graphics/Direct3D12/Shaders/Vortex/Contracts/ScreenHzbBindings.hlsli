//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCREENHZBBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCREENHZBBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct ScreenHzbFrameBindingsData
{
    uint closest_srv;
    uint furthest_srv;
    uint width;
    uint height;
    uint mip_count;
    uint flags;
    float hzb_size_x;
    float hzb_size_y;
    float hzb_view_size_x;
    float hzb_view_size_y;
    int hzb_view_rect_min_x;
    int hzb_view_rect_min_y;
    int hzb_view_rect_width;
    int hzb_view_rect_height;
    float viewport_uv_to_hzb_buffer_uv_x;
    float viewport_uv_to_hzb_buffer_uv_y;
    float hzb_uv_factor_x;
    float hzb_uv_factor_y;
    float hzb_uv_inv_factor_x;
    float hzb_uv_inv_factor_y;
    float hzb_uv_to_screen_uv_scale_x;
    float hzb_uv_to_screen_uv_scale_y;
    float hzb_uv_to_screen_uv_bias_x;
    float hzb_uv_to_screen_uv_bias_y;
    float hzb_base_texel_size_x;
    float hzb_base_texel_size_y;
    float sample_pixel_to_hzb_uv_x;
    float sample_pixel_to_hzb_uv_y;
    float screen_pos_to_hzb_uv_scale_x;
    float screen_pos_to_hzb_uv_scale_y;
    float screen_pos_to_hzb_uv_bias_x;
    float screen_pos_to_hzb_uv_bias_y;
};

static const uint SCREEN_HZB_FLAG_AVAILABLE = (1u << 0u);
static const uint SCREEN_HZB_FLAG_FURTHEST_VALID = (1u << 1u);
static const uint SCREEN_HZB_FLAG_CLOSEST_VALID = (1u << 2u);

static inline ScreenHzbFrameBindingsData MakeInvalidScreenHzbBindings()
{
    ScreenHzbFrameBindingsData bindings = (ScreenHzbFrameBindingsData)0;
    bindings.closest_srv = K_INVALID_BINDLESS_INDEX;
    bindings.furthest_srv = K_INVALID_BINDLESS_INDEX;
    return bindings;
}

static inline ScreenHzbFrameBindingsData LoadScreenHzbBindings(uint slot)
{
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot))
    {
        return MakeInvalidScreenHzbBindings();
    }

    StructuredBuffer<ScreenHzbFrameBindingsData> bindings_buffer
        = ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

static inline bool IsScreenHzbAvailable(ScreenHzbFrameBindingsData bindings)
{
    return (bindings.flags & SCREEN_HZB_FLAG_AVAILABLE) != 0u;
}

static inline bool IsScreenHzbFurthestValid(ScreenHzbFrameBindingsData bindings)
{
    return (bindings.flags & SCREEN_HZB_FLAG_FURTHEST_VALID) != 0u;
}

static inline bool IsScreenHzbClosestValid(ScreenHzbFrameBindingsData bindings)
{
    return (bindings.flags & SCREEN_HZB_FLAG_CLOSEST_VALID) != 0u;
}

static inline float2 GetHzbSize(ScreenHzbFrameBindingsData bindings)
{
    return float2(bindings.hzb_size_x, bindings.hzb_size_y);
}

static inline float2 GetHzbViewSize(ScreenHzbFrameBindingsData bindings)
{
    return float2(bindings.hzb_view_size_x, bindings.hzb_view_size_y);
}

static inline int4 GetHzbViewRect(ScreenHzbFrameBindingsData bindings)
{
    return int4(
        bindings.hzb_view_rect_min_x,
        bindings.hzb_view_rect_min_y,
        bindings.hzb_view_rect_width,
        bindings.hzb_view_rect_height);
}

static inline float2 GetViewportUvToHzbBufferUv(ScreenHzbFrameBindingsData bindings)
{
    return float2(
        bindings.viewport_uv_to_hzb_buffer_uv_x,
        bindings.viewport_uv_to_hzb_buffer_uv_y);
}

static inline float4 GetHzbUvFactorAndInvFactor(ScreenHzbFrameBindingsData bindings)
{
    return float4(
        bindings.hzb_uv_factor_x,
        bindings.hzb_uv_factor_y,
        bindings.hzb_uv_inv_factor_x,
        bindings.hzb_uv_inv_factor_y);
}

static inline float4 GetHzbUvToScreenUvScaleBias(ScreenHzbFrameBindingsData bindings)
{
    return float4(
        bindings.hzb_uv_to_screen_uv_scale_x,
        bindings.hzb_uv_to_screen_uv_scale_y,
        bindings.hzb_uv_to_screen_uv_bias_x,
        bindings.hzb_uv_to_screen_uv_bias_y);
}

static inline float2 GetHzbBaseTexelSize(ScreenHzbFrameBindingsData bindings)
{
    return float2(bindings.hzb_base_texel_size_x, bindings.hzb_base_texel_size_y);
}

static inline float2 GetSamplePixelToHzbUv(ScreenHzbFrameBindingsData bindings)
{
    return float2(bindings.sample_pixel_to_hzb_uv_x, bindings.sample_pixel_to_hzb_uv_y);
}

static inline float4 GetScreenPosToHzbUvScaleBias(ScreenHzbFrameBindingsData bindings)
{
    return float4(
        bindings.screen_pos_to_hzb_uv_scale_x,
        bindings.screen_pos_to_hzb_uv_scale_y,
        bindings.screen_pos_to_hzb_uv_bias_x,
        bindings.screen_pos_to_hzb_uv_bias_y);
}

#endif
