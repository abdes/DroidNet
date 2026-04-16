//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDLIGHTINGCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDLIGHTINGCOMMON_HLSLI

#include "Renderer/ViewConstants.hlsli"

#include "Vortex/Contracts/ViewFrameBindings.hlsli"
#include "Vortex/Shared/DeferredShadingCommon.hlsli"

struct DeferredLightConstants
{
    float4 light_position_and_radius;
    float4 light_color_and_intensity;
    float4 light_direction_and_falloff;
    float4 spot_angles;
    float4x4 light_world_matrix;
    uint light_type;
    uint _padding0;
    uint _padding1;
    uint _padding2;
};

struct DeferredLightVolumeVSOutput
{
    float4 screen_position : TEXCOORD0;
    float4 position : SV_POSITION;
};

static const uint DEFERRED_LIGHT_SPHERE_SLICES = 16u;
static const uint DEFERRED_LIGHT_SPHERE_STACKS = 8u;
static const uint DEFERRED_LIGHT_CONE_SLICES = 24u;
static const float DEFERRED_LIGHT_TWO_PI = 6.28318530718f;

// Phase 03 keeps UE's bounded-volume local-light contract but generates the
// point/spot proxy geometry procedurally from SV_VertexID instead of binding
// persistent sphere/cone vertex buffers.
static inline float3 DeferredLightSphereRingVertex(uint ring, uint slice)
{
    const float phi = PI * ring / DEFERRED_LIGHT_SPHERE_STACKS;
    const float theta = DEFERRED_LIGHT_TWO_PI * slice / DEFERRED_LIGHT_SPHERE_SLICES;
    const float sin_phi = sin(phi);
    return float3(
        sin_phi * cos(theta),
        cos(phi),
        sin_phi * sin(theta));
}

static inline float3 GenerateDeferredLightSphereVertex(uint vertex_id)
{
    const uint triangle_index = vertex_id / 3u;
    const uint corner_index = vertex_id % 3u;
    const uint top_cap_triangle_count = DEFERRED_LIGHT_SPHERE_SLICES;
    const uint middle_triangle_count
        = (DEFERRED_LIGHT_SPHERE_STACKS - 2u)
        * DEFERRED_LIGHT_SPHERE_SLICES * 2u;

    const float3 north_pole = float3(0.0f, 1.0f, 0.0f);
    const float3 south_pole = float3(0.0f, -1.0f, 0.0f);

    if (triangle_index < top_cap_triangle_count) {
        const uint slice = triangle_index;
        const uint next_slice = (slice + 1u) % DEFERRED_LIGHT_SPHERE_SLICES;
        if (corner_index == 0u) {
            return north_pole;
        }
        if (corner_index == 1u) {
            return DeferredLightSphereRingVertex(1u, next_slice);
        }
        return DeferredLightSphereRingVertex(1u, slice);
    }

    if (triangle_index < top_cap_triangle_count + middle_triangle_count) {
        const uint local_triangle = triangle_index - top_cap_triangle_count;
        const uint quad_index = local_triangle / 2u;
        const uint ring = quad_index / DEFERRED_LIGHT_SPHERE_SLICES + 1u;
        const uint slice = quad_index % DEFERRED_LIGHT_SPHERE_SLICES;
        const uint next_slice = (slice + 1u) % DEFERRED_LIGHT_SPHERE_SLICES;

        const float3 v00 = DeferredLightSphereRingVertex(ring, slice);
        const float3 v01 = DeferredLightSphereRingVertex(ring, next_slice);
        const float3 v10 = DeferredLightSphereRingVertex(ring + 1u, slice);
        const float3 v11 = DeferredLightSphereRingVertex(ring + 1u, next_slice);

        if ((local_triangle & 1u) == 0u) {
            if (corner_index == 0u) {
                return v00;
            }
            if (corner_index == 1u) {
                return v10;
            }
            return v01;
        }

        if (corner_index == 0u) {
            return v10;
        }
        if (corner_index == 1u) {
            return v11;
        }
        return v01;
    }

    const uint local_triangle
        = triangle_index - top_cap_triangle_count - middle_triangle_count;
    const uint slice = local_triangle;
    const uint next_slice = (slice + 1u) % DEFERRED_LIGHT_SPHERE_SLICES;
    if (corner_index == 0u) {
        return south_pole;
    }
    if (corner_index == 1u) {
        return DeferredLightSphereRingVertex(
            DEFERRED_LIGHT_SPHERE_STACKS - 1u, slice);
    }
    return DeferredLightSphereRingVertex(
        DEFERRED_LIGHT_SPHERE_STACKS - 1u, next_slice);
}

static inline float3 DeferredLightConeRingVertex(uint slice)
{
    const float theta = DEFERRED_LIGHT_TWO_PI * slice / DEFERRED_LIGHT_CONE_SLICES;
    return float3(cos(theta), -1.0f, sin(theta));
}

static inline float3 GenerateDeferredLightConeVertex(uint vertex_id)
{
    const uint triangle_index = vertex_id / 3u;
    const uint corner_index = vertex_id % 3u;
    const uint side_triangle_count = DEFERRED_LIGHT_CONE_SLICES;
    const uint slice = triangle_index % DEFERRED_LIGHT_CONE_SLICES;
    const uint next_slice = (slice + 1u) % DEFERRED_LIGHT_CONE_SLICES;

    if (triangle_index < side_triangle_count) {
        if (corner_index == 0u) {
            return float3(0.0f, 0.0f, 0.0f);
        }
        if (corner_index == 1u) {
            return DeferredLightConeRingVertex(next_slice);
        }
        return DeferredLightConeRingVertex(slice);
    }

    if (corner_index == 0u) {
        return float3(0.0f, -1.0f, 0.0f);
    }
    if (corner_index == 1u) {
        return DeferredLightConeRingVertex(slice);
    }
    return DeferredLightConeRingVertex(next_slice);
}

static inline SceneTextureBindingData LoadBindingsFromCurrentView()
{
    return LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
}

static inline DeferredLightVolumeVSOutput GenerateDeferredLightVolume(
    float3 local_position, float4x4 light_world_matrix)
{
    DeferredLightVolumeVSOutput output = (DeferredLightVolumeVSOutput)0;
    const float4 world_position = mul(light_world_matrix, float4(local_position, 1.0f));
    const float4 view_position = mul(view_matrix, world_position);
    output.screen_position = mul(projection_matrix, view_position);
    output.position = output.screen_position;
    return output;
}

static inline float2 ResolveDeferredLightScreenUv(float4 screen_position)
{
    const float safe_w = max(abs(screen_position.w), 1.0e-6f);
    if (safe_w <= 0.0f) {
        return 0.0f.xx;
    }

    float2 uv = screen_position.xy / safe_w * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    return uv;
}

static inline float3 LoadDeferredLightColor(float4 light_color_and_intensity)
{
    return light_color_and_intensity.xyz * light_color_and_intensity.www;
}

static inline float ComputeLocalLightDistanceAttenuation(
    float3 light_vector, float light_radius)
{
    const float radius = max(light_radius, EPSILON_SMALL);
    const float distance = length(light_vector);
    if (distance >= radius) {
        return 0.0f;
    }

    float attenuation = saturate(1.0f - pow(distance / radius, 4.0f));
    attenuation = attenuation * attenuation / (distance * distance + 1.0f);
    return attenuation;
}

static inline float ComputeSpotLightAngularAttenuation(
    float3 light_direction_to_source,
    float3 spot_direction_ws,
    float inner_angle_cosine,
    float outer_angle_cosine)
{
    const float inner_cosine = max(inner_angle_cosine, outer_angle_cosine);
    const float angle_span = max(inner_cosine - outer_angle_cosine, EPSILON_SMALL);
    const float cosine_angle = dot(
        -VortexSafeNormalize(light_direction_to_source),
        VortexSafeNormalize(spot_direction_ws));
    const float attenuation = saturate(
        (cosine_angle - outer_angle_cosine) / angle_span);
    return attenuation * attenuation;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDLIGHTINGCOMMON_HLSLI
