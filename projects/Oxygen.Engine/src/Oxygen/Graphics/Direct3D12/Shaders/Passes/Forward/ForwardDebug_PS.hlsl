//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardDebug_PS.hlsl
//! @brief Pixel shader for Forward+ diagnostic and LDR debug visualization modes.

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"
#include "Renderer/DebugHelpers.hlsli"
#include "Renderer/Vertex.hlsli"

#include "MaterialFlags.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Passes/Forward/ForwardPbr.hlsli"
#include "Passes/Forward/ForwardMaterialEval.hlsli"
#include "Passes/Lighting/ClusterLookup.hlsli"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
    bool is_front_face : SV_IsFrontFace;
};

static inline float3 GetIblFaceColor(uint face_index)
{
    switch (face_index)
    {
        case 0: return float3(1.0, 0.0, 0.0); // +X
        case 1: return float3(0.0, 1.0, 0.0); // -X
        case 2: return float3(0.0, 0.0, 1.0); // +Y
        case 3: return float3(1.0, 1.0, 0.0); // -Y
        case 4: return float3(0.0, 1.0, 1.0); // +Z
        case 5: return float3(1.0, 0.0, 1.0); // -Z
        default: return float3(1.0, 1.0, 1.0);
    }
}

static inline void CubemapDirToFaceUv(float3 dir, out uint face_index, out float2 uv)
{
    float3 a = abs(dir);
    float s = 0.0;
    float t = 0.0;

    if (a.x >= a.y && a.x >= a.z)
    {
        if (dir.x >= 0.0)
        {
            face_index = 0u; // +X
            s = -dir.z / a.x;
            t =  dir.y / a.x;
        }
        else
        {
            face_index = 1u; // -X
            s =  dir.z / a.x;
            t =  dir.y / a.x;
        }
    }
    else if (a.y >= a.x && a.y >= a.z)
    {
        if (dir.y >= 0.0)
        {
            face_index = 2u; // +Y
            s =  dir.x / a.y;
            t = -dir.z / a.y;
        }
        else
        {
            face_index = 3u; // -Y
            s =  dir.x / a.y;
            t =  dir.z / a.y;
        }
    }
    else
    {
        if (dir.z >= 0.0)
        {
            face_index = 4u; // +Z
            s =  dir.x / a.z;
            t =  dir.y / a.z;
        }
        else
        {
            face_index = 5u; // -Z
            s = -dir.x / a.z;
            t =  dir.y / a.z;
        }
    }

    uv = float2(0.5 * (s + 1.0), 0.5 * (1.0 - t));
}

static inline float3 MakeIblDebugColor(float3 dir, bool include_grid)
{
    uint face_index = 0u;
    float2 uv = 0.0;
    CubemapDirToFaceUv(normalize(dir), face_index, uv);

    float3 base = GetIblFaceColor(face_index);
    float grid_line = 0.0;
    if (include_grid) {
        float2 grid = frac(uv * 8.0);
        grid_line += step(grid.x, 0.02);
        grid_line += step(grid.y, 0.02);
        grid_line += step(0.98, grid.x);
        grid_line += step(0.98, grid.y);
        grid_line = saturate(grid_line);
    }

    float3 color = lerp(base, float3(1.0, 1.0, 1.0), grid_line);
    return color;
}

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

#ifdef ALPHA_TEST
    if (bindless_draw_metadata_slot != K_INVALID_BINDLESS_INDEX && bindless_material_constants_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];
        StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];
        if ((mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u) {
            const float2 uv = ApplyMaterialUv(input.uv, mat);
            float alpha = 1.0f;
            if (!((mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u) && mat.opacity_texture_index != K_INVALID_BINDLESS_INDEX) {
                Texture2D<float4> opacity_tex = ResourceDescriptorHeap[mat.opacity_texture_index];
                alpha = opacity_tex.Sample(linear_sampler, uv).a;
            }
            clip(alpha - (mat.alpha_cutoff <= 0.0f ? 0.5f : mat.alpha_cutoff));
        }
    }
#endif

    float3 debug_out = 0.0f;
    bool debug_handled = false;

#if defined(DEBUG_UV0)
    debug_out = float3(frac(input.uv), 0.0f); debug_handled = true;
#elif defined(DEBUG_BASE_COLOR) || defined(DEBUG_OPACITY) || defined(DEBUG_WORLD_NORMALS) || defined(DEBUG_ROUGHNESS) || defined(DEBUG_METALNESS)
    MaterialSurface s = EvaluateMaterialSurface(input.world_pos, input.world_normal, input.world_tangent, input.world_bitangent, input.uv, g_DrawIndex, input.is_front_face);
    #if defined(DEBUG_BASE_COLOR)
        debug_out = s.base_rgb * input.color;
    #elif defined(DEBUG_OPACITY)
        debug_out = s.base_a.xxx;
    #elif defined(DEBUG_WORLD_NORMALS)
        debug_out = s.N * 0.5f + 0.5f;
    #elif defined(DEBUG_ROUGHNESS)
        debug_out = s.roughness.xxx;
    #elif defined(DEBUG_METALNESS)
        debug_out = s.metalness.xxx;
    #endif
    debug_handled = true;
#elif defined(DEBUG_IBL_SPECULAR) || defined(DEBUG_IBL_RAW_SKY) || defined(DEBUG_IBL_IRRADIANCE) || defined(DEBUG_IBL_FACE_INDEX)
    const float3 N_v = SafeNormalize(input.world_normal);
    const float3 V_v = SafeNormalize(camera_position - input.world_pos);
    const float3 cube_R = CubemapSamplingDirFromOxygenWS(reflect(-V_v, N_v));
    const float3 cube_N = CubemapSamplingDirFromOxygenWS(N_v);

    if (dot(N_v, N_v) < 0.5 || dot(V_v, V_v) < 0.5) {
        debug_out = float3(1.0, 0.0, 1.0);
        debug_handled = true;
    }

    if (!debug_handled) {
        #if defined(DEBUG_IBL_SPECULAR)
            debug_out = MakeIblDebugColor(cube_R, true);
            debug_handled = true;
        #elif defined(DEBUG_IBL_IRRADIANCE)
            const float screen_w = max(
                1.0, (float)EnvironmentDynamicData.cluster_dim_x
                      * (float)EnvironmentDynamicData.tile_size_px);
            const bool show_world = (input.position.x < 0.5 * screen_w);
            if (show_world) {
                debug_out = input.world_normal * 0.5 + 0.5;
            } else {
                debug_out = MakeIblDebugColor(cube_N, false);
            }
            debug_handled = true;
        #elif defined(DEBUG_IBL_FACE_INDEX)
            {
                uint face_index = 0u;
                float2 uv = 0.0;
                CubemapDirToFaceUv(normalize(cube_R), face_index, uv);
                debug_out = GetIblFaceColor(face_index);
                debug_handled = true;
            }
        #else
            EnvironmentStaticData env_data;
            if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data) && env_data.sky_light.enabled) {
                uint slot = env_data.sky_light.cubemap_slot;
                if (slot == K_INVALID_BINDLESS_INDEX) slot = env_data.sky_sphere.cubemap_slot;
                if (slot != K_INVALID_BINDLESS_INDEX) {
                    TextureCube<float4> sky_cube = ResourceDescriptorHeap[slot];
                    float3 raw = sky_cube.SampleLevel(linear_sampler, cube_R, 0.0).rgb;
                    debug_out = raw * env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
                    debug_handled = true;
                }
            }
        #endif
    }
#endif

    if (!debug_handled) {
        const uint grid = EnvironmentDynamicData.bindless_cluster_grid_slot;
        if (grid != K_INVALID_BINDLESS_INDEX) {
            float linear_depth = max(-mul(view_matrix, float4(input.world_pos, 1.0)).z, 0.0);
            uint idx = GetClusterIndex(input.position.xy, linear_depth);
            uint3 dims = GetClusterDimensions();
            #if defined(DEBUG_LIGHT_HEATMAP)
                debug_out = HeatMapColor(saturate((float)GetClusterLightInfo(grid, idx).light_count / 48.0f));
            #elif defined(DEBUG_DEPTH_SLICE)
                debug_out = DepthSliceColor(idx / (dims.x * dims.y), dims.z);
            #elif defined(DEBUG_CLUSTER_INDEX)
                debug_out = ClusterIndexColor(uint3(uint(input.position.x)/EnvironmentDynamicData.tile_size_px, uint(input.position.y)/EnvironmentDynamicData.tile_size_px, idx/(dims.x*dims.y)));
            #endif
        }
    }

#if defined(DEBUG_IBL_RAW_SKY)
    debug_out *= GetExposure();
#endif

#ifdef OXYGEN_HDR_OUTPUT
    return float4(debug_out, 1.0f);
#else
    return float4(LinearToSrgb(debug_out), 1.0f);
#endif
}
