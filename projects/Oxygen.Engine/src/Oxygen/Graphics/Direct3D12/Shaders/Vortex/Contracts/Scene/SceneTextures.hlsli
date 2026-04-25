//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTURES_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTURES_HLSLI

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Scene/SceneTextureBindings.hlsli"

static inline float SampleSceneDepth(float2 uv, SceneTextureBindingData bindings)
{
    if (bindings.scene_depth_srv == INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2D<float> depth_texture = ResourceDescriptorHeap[bindings.scene_depth_srv];
    SamplerState point_clamp_sampler
        = SamplerDescriptorHeap[VORTEX_SAMPLER_POINT_CLAMP];
    return depth_texture.SampleLevel(point_clamp_sampler, uv, 0.0f);
}

static inline float4 SampleSceneColor(float2 uv, SceneTextureBindingData bindings)
{
    if (bindings.scene_color_srv == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    Texture2D<float4> scene_color = ResourceDescriptorHeap[bindings.scene_color_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return scene_color.SampleLevel(linear_sampler, uv, 0.0f);
}

static inline float4 LoadSceneColor(uint2 pixel_coord, SceneTextureBindingData bindings)
{
    if (bindings.scene_color_srv == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    Texture2D<float4> scene_color = ResourceDescriptorHeap[bindings.scene_color_srv];
    return scene_color.Load(int3(pixel_coord, 0));
}

static inline float4 SampleGBuffer(
    uint gbuffer_index, float2 uv, SceneTextureBindingData bindings)
{
    if (gbuffer_index >= GBUFFER_ACTIVE_COUNT) {
        return 0.0f.xxxx;
    }

    const uint slot = bindings.gbuffer_srvs[gbuffer_index];
    if (slot == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    Texture2D<float4> gbuffer_texture = ResourceDescriptorHeap[slot];
    SamplerState point_clamp_sampler
        = SamplerDescriptorHeap[VORTEX_SAMPLER_POINT_CLAMP];
    return gbuffer_texture.SampleLevel(point_clamp_sampler, uv, 0.0f);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTURES_HLSLI
