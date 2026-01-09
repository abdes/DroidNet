//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardMesh_PS.hlsl
//! @brief Pixel shader for Forward+ mesh rendering with debug visualization modes.
//!
//! This shader handles PBR lighting as well as debug visualization modes
//! controlled via boolean defines:
//!
//! Debug defines (mutually exclusive):
//!   (none): Normal PBR rendering
//!   DEBUG_LIGHT_HEATMAP: Heat map of lights per cluster (black->green->yellow->red)
//!   DEBUG_DEPTH_SLICE: Visualize depth slices with distinct cycling colors
//!   DEBUG_CLUSTER_INDEX: Visualize cluster boundaries as checkerboard

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/AtmosphereHelpers.hlsli"
#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"

#include "MaterialFlags.hlsli"

// Define vertex structure to match the CPU-side Vertex struct
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#include "Core/Bindless/BindlessHelpers.hlsl"

// Extracted systems
#include "Passes/Forward/ForwardPbr.hlsli"
#include "Passes/Forward/ForwardMaterialEval.hlsli"
#include "Passes/Forward/ForwardDirectLighting.hlsli"
#include "Passes/Lighting/ClusterLookup.hlsli"

// Approximate split-sum specular BRDF (no LUT required).
float3 EnvBrdfApprox(float3 F0, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Vertex shader output / Pixel shader input (must match ForwardMesh_VS.hlsl)
struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
};

//=== Debug Visualization Helpers ===----------------------------------------//

// Check if any debug mode is active
#if defined(DEBUG_LIGHT_HEATMAP) || defined(DEBUG_DEPTH_SLICE) || defined(DEBUG_CLUSTER_INDEX)
#define DEBUG_MODE_ACTIVE 1
#endif

#ifdef DEBUG_MODE_ACTIVE

// Convert a value [0,1] to a heat map color (black -> green -> yellow -> red)
float3 HeatMapColor(float t) {
    t = saturate(t);

    // 4-color gradient: black (0) -> green -> yellow -> red (1)
    float3 colors[4] = {
        float3(0.0, 0.0, 0.0),  // Black (no lights)
        float3(0.0, 1.0, 0.0),  // Green (few lights)
        float3(1.0, 1.0, 0.0),  // Yellow (moderate)
        float3(1.0, 0.0, 0.0)   // Red (many lights)
    };

    float segment = t * 3.0;
    uint idx = (uint)floor(segment);
    float localT = frac(segment);
    idx = min(idx, 2);

    return lerp(colors[idx], colors[idx + 1], localT);
}

// Rainbow}

// Visualize depth slice as a rainbow gradient
float3 DepthSliceColor(uint slice, uint max_slices) {
    if (max_slices <= 1) {
        return float3(0.5, 0.5, 0.5); // Gray for tile-based (no depth slices)
    }

    // 8 distinct colors, cycling through them
    // NO BLUE - only warm/neutral colors
    static const float3 colors[8] = {
        float3(1.0, 0.0, 0.0),   // Red
        float3(1.0, 0.5, 0.0),   // Orange
        float3(1.0, 1.0, 0.0),   // Yellow
        float3(0.0, 1.0, 0.0),   // Green
        float3(1.0, 0.0, 0.5),   // Pink
        float3(0.5, 0.0, 0.0),   // Dark Red
        float3(0.0, 0.5, 0.0),   // Dark Green
        float3(1.0, 1.0, 0.5),   // Light Yellow
    };
    return colors[slice % 8];
}

// Visualize cluster index as a checkerboard pattern with color
float3 ClusterIndexColor(uint3 cluster_id) {
    // Create a 3D checkerboard pattern
    uint checker = (cluster_id.x + cluster_id.y + cluster_id.z) % 2;

    // Use cluster ID components for RGB tint
    float r = frac(float(cluster_id.x) * 0.1234);
    float g = frac(float(cluster_id.y) * 0.5678);
    float b = frac(float(cluster_id.z) * 0.9012);

    float3 base_color = float3(r, g, b);
    return lerp(base_color * 0.5, base_color, float(checker));
}

#endif // DEBUG_MODE_ACTIVE

//=== Pixel Shader ==========================================================//

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    // -------------------------------------------------------------------------
    // ALPHA_TEST permutation: Alpha-test for cutout materials.
    // -------------------------------------------------------------------------
#ifdef ALPHA_TEST
    if (bindless_draw_metadata_slot != K_INVALID_BINDLESS_INDEX
        && bindless_material_constants_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer =
            ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

        StructuredBuffer<MaterialConstants> materials =
            ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];

        const bool alpha_test_enabled =
            (mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
        if (alpha_test_enabled) {
            const bool no_texture_sampling =
                (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

            const float2 uv = input.uv * mat.uv_scale + mat.uv_offset;

            float alpha = 1.0f;
            if (!no_texture_sampling
                && mat.opacity_texture_index != K_INVALID_BINDLESS_INDEX) {
                Texture2D<float4> opacity_tex =
                    ResourceDescriptorHeap[mat.opacity_texture_index];
                SamplerState samp = SamplerDescriptorHeap[0];
                alpha = opacity_tex.Sample(samp, uv).a;
            }

            float cutoff = mat.alpha_cutoff;
            if (cutoff <= 0.0f) {
                cutoff = 0.5f;
            }
            clip(alpha - cutoff);
        }
    }
#endif // ALPHA_TEST

    //=== Debug Visualization Modes ===---------------------------------------//

#ifdef DEBUG_MODE_ACTIVE
    // Common cluster lookup for all light culling debug modes
    const uint cluster_grid_slot = EnvironmentDynamicData.bindless_cluster_grid_slot;

        if (cluster_grid_slot == K_INVALID_BINDLESS_INDEX) {
        return float4(1.0, 0.0, 1.0, 1.0); // Magenta = no cluster grid
    }

    const float3 view_pos = mul(view_matrix, float4(input.world_pos, 1.0)).xyz;
    const float linear_depth = max(-view_pos.z, 0.0);

    const uint cluster_index = GetClusterIndex(input.position.xy, linear_depth);
    ClusterLightInfo cluster_info = GetClusterLightInfo(cluster_grid_slot, cluster_index);

    // Compute tile/slice for visualization
    const uint3 cluster_dims = GetClusterDimensions();
    const uint tile_x = uint(input.position.x) / EnvironmentDynamicData.tile_size_px;
    const uint tile_y = uint(input.position.y) / EnvironmentDynamicData.tile_size_px;
    const uint z_slice = cluster_index / (cluster_dims.x * cluster_dims.y);

#ifdef DEBUG_LIGHT_HEATMAP
    // Heat map: light count (black -> green -> yellow -> red)
    const uint max_lights = 48;
    float t = saturate((float)cluster_info.light_count / (float)max_lights);
    return float4(HeatMapColor(t), 1.0);
#endif

#ifdef DEBUG_DEPTH_SLICE
    // Depth slice visualization (rainbow gradient)
    return float4(DepthSliceColor(z_slice, cluster_dims.z), 1.0);
#endif

#ifdef DEBUG_CLUSTER_INDEX
    // Cluster index checkerboard
    return float4(ClusterIndexColor(uint3(tile_x, tile_y, z_slice)), 1.0);
#endif

#else
    //=== Normal PBR Rendering Path ===---------------------------------------//

    float3 N = SafeNormalize(input.world_normal);
    float3 T = input.world_tangent;
    float3 B = input.world_bitangent;

    // Fix degenerate tangents at runtime
    if (dot(T, T) < 1e-6) {
        float3 axis = (abs(N.z) < 0.9) ? float3(0, 0, 1) : float3(1, 0, 0);
        T = normalize(cross(N, axis));
        B = normalize(cross(N, T));
    }
    T = SafeNormalize(T);
    if (dot(B, B) < 1e-6) {
        B = normalize(cross(N, T));
    }
    B = SafeNormalize(B);

    float3 V = SafeNormalize(camera_position - input.world_pos);

    MaterialSurface surf = EvaluateMaterialSurface(
        input.world_pos,
        input.world_normal,
        input.world_tangent,
        input.world_bitangent,
        input.uv,
        g_DrawIndex);

    const float3 base_rgb = surf.base_rgb;
    const float  metalness = surf.metalness;
    const float  roughness = surf.roughness;
    N = surf.N;
    V = surf.V;
    const float NdotV = saturate(dot(N, V));

    // Direct lighting (GGX specular + Lambert diffuse)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, base_rgb, metalness);

    float3 direct = float3(0.0, 0.0, 0.0);
    direct += AccumulateDirectionalLights(N, V, NdotV, F0, base_rgb, metalness, roughness);

    // Use clustered lighting for positional lights
    const float3 view_pos = mul(view_matrix, float4(input.world_pos, 1.0)).xyz;
    const float linear_depth = max(-view_pos.z, 0.0);
        direct += AccumulatePositionalLightsClustered(
        input.world_pos,
        input.position.xy,
        linear_depth,
        N, V, NdotV, F0,
        base_rgb, metalness, roughness);

    EnvironmentStaticData env_data;

    // Image-based lighting (sky light) for ambient + specular
    float3 ibl_diffuse = 0.0f;
    float3 ibl_specular = 0.0f;
    bool has_brdf_lut = false;
    Texture2D<float2> brdf_lut;

    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.sky_light.enabled
        && env_data.sky_light.cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[env_data.sky_light.cubemap_slot];
        SamplerState linear_sampler = SamplerDescriptorHeap[0];
        has_brdf_lut = env_data.sky_light.brdf_lut_slot != K_INVALID_BINDLESS_INDEX;
        if (has_brdf_lut)
        {
            brdf_lut = ResourceDescriptorHeap[env_data.sky_light.brdf_lut_slot];
        }

        uint cube_width, cube_height, cube_levels;
        sky_cube.GetDimensions(0, cube_width, cube_height, cube_levels);
        const float max_mip = max(0.0f, (float)cube_levels - 1.0f);

        const float3 R = reflect(-V, N);
        // Approximate specular prefilter using mip selection from roughness
        ibl_specular = sky_cube.SampleLevel(linear_sampler, R, max_mip * roughness).rgb;

        // Diffuse approximation from lowest mip
        ibl_diffuse = sky_cube.SampleLevel(linear_sampler, N, max_mip).rgb;

        const float3 sky_tint = env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
        ibl_diffuse *= sky_tint * env_data.sky_light.diffuse_intensity;
        ibl_specular *= sky_tint * env_data.sky_light.specular_intensity;

        if (has_brdf_lut)
        {
            const float2 brdf_terms = brdf_lut.Sample(linear_sampler, float2(NdotV, roughness)).rg;
            ibl_specular *= F0 * brdf_terms.x + brdf_terms.y;
        }
    }

    const float3 ambient = ibl_diffuse * base_rgb * (1.0f - metalness);
    const float3 spec_brdf = EnvBrdfApprox(F0, roughness, NdotV);
    const float3 shaded = (direct + ibl_specular * (has_brdf_lut ? 1.0f : spec_brdf) + ambient) * input.color;

    // Emissive
    float3 final_color = shaded + surf.emissive;

    // Apply camera exposure
    final_color *= GetExposure();

    // Apply atmospheric fog
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.fog.enabled)
    {
        // Use designated sun direction from dynamic environment data.
        float3 sun_dir = EnvironmentDynamicData.sun_direction_ws;
        if (EnvironmentDynamicData.sun_valid == 0)
        {
            sun_dir = float3(0.5f, 0.707f, 0.5f);
        }
        sun_dir = normalize(sun_dir);

        AtmosphericFogResult fog = GetAtmosphericFog(
            env_data,
            input.world_pos,
            camera_position,
            sun_dir);

        final_color = ApplyAtmosphericFog(final_color, fog);
    }

#ifdef ALPHA_TEST
    return float4(LinearToSrgb(final_color), 1.0f);
#else
    return float4(LinearToSrgb(final_color), surf.base_a);
#endif

#endif // DEBUG_MODE_ACTIVE
}
