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
    float4 position : SV_POSITION;
    float2 screen_uv : TEXCOORD0;
};

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
    output.position = mul(projection_matrix, view_position);

    const float safe_w = max(abs(output.position.w), 1.0e-6f);
    output.screen_uv = output.position.xy / safe_w * 0.5f + 0.5f;
    output.screen_uv.y = 1.0f - output.screen_uv.y;
    return output;
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
