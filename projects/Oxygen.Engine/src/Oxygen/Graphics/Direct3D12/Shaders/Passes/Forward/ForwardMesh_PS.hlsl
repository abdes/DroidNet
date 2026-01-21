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
//!   DEBUG_IBL_SPECULAR: Visualize IBL specular sampling (prefilter map)
//!   DEBUG_IBL_RAW_SKY: Visualize raw sky cubemap sampling (no prefilter)
//!   DEBUG_IBL_RAW_SKY_VIEWDIR: Visualize raw sky cubemap (view direction)
//!   DEBUG_BASE_COLOR: Visualize base color texture (albedo)
//!   DEBUG_UV0: Visualize UV0 coordinates
//!   DEBUG_OPACITY: Visualize base alpha/opacity

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/AerialPerspective.hlsli"
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
#if defined(DEBUG_LIGHT_HEATMAP) || defined(DEBUG_DEPTH_SLICE) || defined(DEBUG_CLUSTER_INDEX) || defined(DEBUG_IBL_SPECULAR) || defined(DEBUG_IBL_RAW_SKY) || defined(DEBUG_IBL_RAW_SKY_VIEWDIR) || defined(DEBUG_BASE_COLOR) || defined(DEBUG_UV0) || defined(DEBUG_OPACITY)
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

            const float2 uv = ApplyMaterialUv(input.uv, mat);

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
#if defined(DEBUG_UV0)
    const float2 uv = frac(input.uv);
    return float4(uv, 0.0f, 1.0f);
#elif defined(DEBUG_BASE_COLOR)
    const MaterialSurface s = EvaluateMaterialSurface(input.world_pos,
        input.world_normal, input.world_tangent, input.world_bitangent,
        input.uv, g_DrawIndex);
    return float4(s.base_rgb, 1.0f);
#elif defined(DEBUG_OPACITY)
    const MaterialSurface s = EvaluateMaterialSurface(input.world_pos,
        input.world_normal, input.world_tangent, input.world_bitangent,
        input.uv, g_DrawIndex);
    return float4(s.base_a.xxx, 1.0f);
#elif defined(DEBUG_IBL_SPECULAR)
    // IBL specular visualization: sample the prefilter map (if available) and
    // show it directly.
    // Important: keep this mode purely geometric to avoid normal-map / TBN
    // issues obscuring cubemap direction bugs.
    const float3 N = SafeNormalize(input.world_normal);
    const float3 V = SafeNormalize(camera_position - input.world_pos);
    const float roughness = 0.0f; // mirror to make direction errors obvious

    EnvironmentStaticData env_data;
    float3 ibl_specular = 0.0f;

    uint ibl_cubemap_slot = K_INVALID_BINDLESS_INDEX;
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.sky_light.enabled)
    {
        ibl_cubemap_slot = env_data.sky_light.cubemap_slot;
        if (ibl_cubemap_slot == K_INVALID_BINDLESS_INDEX
            && env_data.sky_sphere.enabled
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX)
        {
            ibl_cubemap_slot = env_data.sky_sphere.cubemap_slot;
        }
    }

    if (ibl_cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[ibl_cubemap_slot];
        SamplerState linear_sampler = SamplerDescriptorHeap[0];

        const float3 R = reflect(-V, N);
        const float3 cube_R = CubemapSamplingDirFromOxygenWS(R);

        if (env_data.sky_light.prefilter_map_slot != K_INVALID_BINDLESS_INDEX)
        {
            TextureCube<float4> pref_map = ResourceDescriptorHeap[env_data.sky_light.prefilter_map_slot];
            // For mirror roughness=0, sample mip 0.
            ibl_specular = pref_map.SampleLevel(linear_sampler, cube_R, 0.0f).rgb;
        }
        else
        {
            // Fallback to skybox sampling if no prefilter map is available.
            ibl_specular = sky_cube.SampleLevel(linear_sampler, cube_R, 0.0f).rgb;
        }

        const float3 sky_tint = env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
        ibl_specular *= sky_tint * env_data.sky_light.specular_intensity;
    }

    // Scale HDR down for a readable visualization.
    return float4(ibl_specular * 0.1f, 1.0f);
#elif defined(DEBUG_IBL_RAW_SKY)
    // Raw sky cubemap visualization: sample the source cubemap directly.
    //
    // If this looks distorted, the issue is in cubemap sampling/orientation
    // (direction swizzle, face order/rotation, or source cubemap cooking).
    // Important: use the geometric world normal and view vector here.
    // This avoids any normal-map/tangent-space issues and isolates cubemap
    // sampling and source cubemap correctness.
    const float3 N = SafeNormalize(input.world_normal);
    const float3 V = SafeNormalize(camera_position - input.world_pos);

    EnvironmentStaticData env_data;
    float3 raw_sky = 0.0f;

    uint ibl_cubemap_slot = K_INVALID_BINDLESS_INDEX;
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.sky_light.enabled)
    {
        ibl_cubemap_slot = env_data.sky_light.cubemap_slot;
        if (ibl_cubemap_slot == K_INVALID_BINDLESS_INDEX
            && env_data.sky_sphere.enabled
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX)
        {
            ibl_cubemap_slot = env_data.sky_sphere.cubemap_slot;
        }
    }

    if (ibl_cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[ibl_cubemap_slot];
        SamplerState linear_sampler = SamplerDescriptorHeap[0];

        const float3 R = reflect(-V, N);
        const float3 cube_R = CubemapSamplingDirFromOxygenWS(R);

        raw_sky = sky_cube.SampleLevel(linear_sampler, cube_R, 0).rgb;

        const float3 sky_tint = env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
        raw_sky *= sky_tint;
    }

    // Scale HDR down for a readable visualization.
    return float4(raw_sky * 0.1f, 1.0f);
#elif defined(DEBUG_IBL_RAW_SKY_VIEWDIR)
    // Raw sky cubemap sampled using the camera ray direction.
    //
    // This is the same concept as a skybox: sample by the camera ray through
    // the pixel, not by (camera_position - world_pos) which depends on the
    // shaded surface position and will look like a 'transparent sphere'.
    // If this doesn't match the sky background, the cubemap cook and/or
    // CubemapSamplingDirFromOxygenWS mapping is wrong.

    EnvironmentStaticData env_data;
    float3 raw_sky = 0.0f;

    uint ibl_cubemap_slot = K_INVALID_BINDLESS_INDEX;
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.sky_light.enabled)
    {
        ibl_cubemap_slot = env_data.sky_light.cubemap_slot;
        if (ibl_cubemap_slot == K_INVALID_BINDLESS_INDEX
            && env_data.sky_sphere.enabled
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX)
        {
            ibl_cubemap_slot = env_data.sky_sphere.cubemap_slot;
        }
    }

    if (ibl_cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[ibl_cubemap_slot];
        SamplerState linear_sampler = SamplerDescriptorHeap[0];

        const float2 screen_dims = float2(
            (float)(EnvironmentDynamicData.cluster_dim_x * EnvironmentDynamicData.tile_size_px),
            (float)(EnvironmentDynamicData.cluster_dim_y * EnvironmentDynamicData.tile_size_px));

        // Convert SV_Position.xy (pixel coords) to NDC.
        const float2 uv = input.position.xy / max(screen_dims, 1.0.xx);
        const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

        // Reconstruct a view-space ray from NDC.
        // View space is right-handed in this renderer (forward is -Z), see
        // linear_depth = max(-view_pos.z, 0).
        const float proj_x = projection_matrix[0][0];
        const float proj_y = projection_matrix[1][1];
        const float3 ray_vs = SafeNormalize(
            float3(ndc.x / max(proj_x, 1e-6f), ndc.y / max(proj_y, 1e-6f), -1.0f));

        // Transform ray to world space using inverse view rotation.
        const float3x3 view_rot = (float3x3)view_matrix;
        const float3 ray_ws = SafeNormalize(mul(transpose(view_rot), ray_vs));

        const float3 cube_dir = CubemapSamplingDirFromOxygenWS(ray_ws);
        raw_sky = sky_cube.SampleLevel(linear_sampler, cube_dir, 0).rgb;

        const float3 sky_tint = env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
        raw_sky *= sky_tint;
    }

    return float4(raw_sky * 0.1f, 1.0f);
#endif

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
    const float3 N = surf.N;
    const float3 V = surf.V;
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

    uint ibl_cubemap_slot = K_INVALID_BINDLESS_INDEX;
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && env_data.sky_light.enabled)
    {
        ibl_cubemap_slot = env_data.sky_light.cubemap_slot;
        if (ibl_cubemap_slot == K_INVALID_BINDLESS_INDEX
            && env_data.sky_sphere.enabled
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX)
        {
            ibl_cubemap_slot = env_data.sky_sphere.cubemap_slot;
        }
    }

    if (ibl_cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[ibl_cubemap_slot];
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

        const float3 cube_R = CubemapSamplingDirFromOxygenWS(R);
        const float3 cube_N = CubemapSamplingDirFromOxygenWS(N);

        // Check for generated IBL maps (Irradiance + Prefilter)
        if (env_data.sky_light.irradiance_map_slot != K_INVALID_BINDLESS_INDEX
            && env_data.sky_light.prefilter_map_slot != K_INVALID_BINDLESS_INDEX)
        {
             TextureCube<float4> irr_map = ResourceDescriptorHeap[env_data.sky_light.irradiance_map_slot];
             TextureCube<float4> pref_map = ResourceDescriptorHeap[env_data.sky_light.prefilter_map_slot];

             // Diffuse from Irradiance Map (Mip 0)
             ibl_diffuse = irr_map.SampleLevel(linear_sampler, cube_N, 0).rgb;

             // Specular from Prefilter Map
             uint pf_w, pf_h, pf_levels;
             pref_map.GetDimensions(0, pf_w, pf_h, pf_levels);
             float pf_max_mip = max(0.0f, (float)pf_levels - 1.0f);

             ibl_specular = pref_map.SampleLevel(linear_sampler, cube_R, pf_max_mip * roughness).rgb;
        }
        else
        {
            // Fallback: Approximate from raw skybox (Incorrect but legacy behavior)
            ibl_specular = sky_cube.SampleLevel(linear_sampler, cube_R, max_mip * roughness).rgb;
            ibl_diffuse = sky_cube.SampleLevel(linear_sampler, cube_N, max_mip).rgb;
        }

        const float3 sky_tint = env_data.sky_light.tint_rgb * env_data.sky_light.intensity;
        ibl_diffuse *= sky_tint * env_data.sky_light.diffuse_intensity;
        ibl_specular *= sky_tint * env_data.sky_light.specular_intensity;
    }

    const float3 ambient = ibl_diffuse * base_rgb * (1.0f - metalness);
    const float3 spec_brdf = EnvBrdfApprox(F0, roughness, NdotV);
    float3 ibl_specular_term = ibl_specular * spec_brdf;

    // If a BRDF LUT is provided, prefer the LUT-based split-sum.
    // Defensive fallback: if the LUT samples as ~0 (or NaN), keep the analytic
    // approximation so IBL specular doesn't disappear.
    if (has_brdf_lut && ibl_cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        SamplerState linear_sampler = SamplerDescriptorHeap[0];
        const float2 brdf = brdf_lut.SampleLevel(linear_sampler, float2(NdotV, roughness), 0).rg;

        const bool brdf_is_nan = any(isnan(brdf));
        const bool brdf_is_effectively_zero = max(brdf.x, brdf.y) <= 1e-5;
        if (!brdf_is_nan && !brdf_is_effectively_zero)
        {
            ibl_specular_term = ibl_specular * (F0 * brdf.x + brdf.y);
        }
    }

    const float3 shaded = (direct + ibl_specular_term + ambient) * input.color;

    // Emissive
    float3 final_color = shaded + surf.emissive;

    // Apply aerial perspective atmospheric effects.
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        if (ShouldUseLutAerialPerspective(env_data.atmosphere))
        {
            // Get designated sun direction from dynamic environment data.
            // GetSunDirectionWS() returns override direction if enabled.
            float3 sun_dir = GetSunDirectionWS();
            // Only use fallback if there's no valid sun AND no override enabled
            if (EnvironmentDynamicData.sun_valid == 0 && !IsOverrideSunEnabled())
            {
                sun_dir = float3(0.5f, 0.707f, 0.5f);
            }
            sun_dir = normalize(sun_dir);

            AerialPerspectiveResult ap = ComputeAerialPerspective(
                env_data,
                input.world_pos,
                camera_position,
                sun_dir);

            final_color = ApplyAerialPerspective(final_color, ap);
        }
    }

    // Apply camera exposure after lighting + atmosphere.
    final_color *= GetExposure();

#ifdef ALPHA_TEST
    return float4(LinearToSrgb(final_color), 1.0f);
#else
    return float4(LinearToSrgb(final_color), surf.base_a);
#endif

#endif // DEBUG_MODE_ACTIVE
}
