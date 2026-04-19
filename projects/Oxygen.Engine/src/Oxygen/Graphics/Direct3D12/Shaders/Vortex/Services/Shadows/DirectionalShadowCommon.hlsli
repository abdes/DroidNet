//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Renderer/ViewConstants.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"

static const uint VORTEX_SHADOW_TECHNIQUE_DIRECTIONAL_CONVENTIONAL = 1u << 0u;

struct VortexShadowCascadeBinding
{
    float4x4 light_view_projection;
    float split_near;
    float split_far;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
};

struct VortexShadowFrameBindings
{
    uint conventional_shadow_surface_handle;
    uint cascade_count;
    uint technique_flags;
    uint sampling_contract_flags;
    VortexShadowCascadeBinding cascades[4];
};

static inline VortexShadowFrameBindings MakeInvalidVortexShadowFrameBindings()
{
    VortexShadowFrameBindings bindings = (VortexShadowFrameBindings)0;
    bindings.conventional_shadow_surface_handle = INVALID_BINDLESS_INDEX;
    bindings.cascade_count = 0u;
    bindings.technique_flags = 0u;
    bindings.sampling_contract_flags = 0u;
    return bindings;
}

static inline VortexShadowFrameBindings LoadVortexShadowFrameBindings()
{
    const ViewFrameBindingsData view_bindings =
        LoadVortexViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.shadow_frame_slot == INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(view_bindings.shadow_frame_slot)) {
        return MakeInvalidVortexShadowFrameBindings();
    }

    StructuredBuffer<VortexShadowFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[view_bindings.shadow_frame_slot];
    return bindings_buffer[0];
}

static inline uint SelectDirectionalShadowCascade(
    VortexShadowFrameBindings bindings,
    float view_depth)
{
    const uint cascade_count = min(max(bindings.cascade_count, 1u), 4u);
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        if (i >= cascade_count) {
            break;
        }
        if (view_depth <= bindings.cascades[i].split_far) {
            return i;
        }
    }
    return cascade_count - 1u;
}

static inline float SampleDirectionalShadowSurface(
    VortexShadowFrameBindings bindings,
    uint cascade_index,
    float2 shadow_uv,
    float receiver_depth)
{
    if (bindings.conventional_shadow_surface_handle == INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(bindings.conventional_shadow_surface_handle)) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[bindings.conventional_shadow_surface_handle];
    const uint layer =
        (uint)(bindings.cascades[cascade_index].sampling_metadata0.x + 0.5f);
    const float2 inverse_resolution =
        max(bindings.cascades[cascade_index].sampling_metadata0.yz, float2(0.000001f, 0.000001f));
    const float2 texel_size = inverse_resolution;
    const float2 texture_size = 1.0f / texel_size;
    const int2 max_coord =
        max(int2(texture_size) - int2(1, 1), int2(0, 0));
    const int2 center = int2(shadow_uv * texture_size);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 coord = clamp(center + int2(x, y), int2(0, 0), max_coord);
            const float stored_depth = shadow_surface.Load(int4(coord, (int)layer, 0));
            visibility += receiver_depth <= stored_depth + 0.0008f ? 1.0f : 0.0f;
        }
    }

    return visibility * (1.0f / 9.0f);
}

static inline float ComputeDirectionalShadowVisibility(
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    if ((bindings.technique_flags & VORTEX_SHADOW_TECHNIQUE_DIRECTIONAL_CONVENTIONAL) == 0u
        || bindings.cascade_count == 0u
        || bindings.conventional_shadow_surface_handle == INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    const float view_depth = max(0.0f, -mul(view_matrix, float4(world_position, 1.0f)).z);
    const uint cascade_index = SelectDirectionalShadowCascade(bindings, view_depth);
    const VortexShadowCascadeBinding cascade = bindings.cascades[cascade_index];

    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : float3(0.0f, -1.0f, 0.0f));
    const float ndotl = saturate(dot(safe_normal, safe_light_dir));
    const float3 biased_world_position =
        world_position + safe_normal * 0.02f + safe_light_dir * lerp(0.0012f, 0.0004f, ndotl);

    const float4 shadow_clip =
        mul(cascade.light_view_projection, float4(biased_world_position, 1.0f));
    if (abs(shadow_clip.w) <= 1.0e-6f) {
        return 1.0f;
    }

    const float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    const float2 shadow_uv = float2(
        shadow_ndc.x * 0.5f + 0.5f,
        shadow_ndc.y * -0.5f + 0.5f);
    if (shadow_uv.x < 0.0f || shadow_uv.x > 1.0f
        || shadow_uv.y < 0.0f || shadow_uv.y > 1.0f
        || shadow_ndc.z < 0.0f || shadow_ndc.z > 1.0f) {
        return 1.0f;
    }

    return SampleDirectionalShadowSurface(
        bindings, cascade_index, shadow_uv, shadow_ndc.z);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI
