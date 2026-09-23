//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Lighting/ForwardLocalLightRecord.hlsli"
#include "Vortex/Services/Lighting/LocalLightAttenuation.hlsli"
#include "Vortex/Services/Lighting/DeferredShadingCommon.hlsli"
#define DistributionGGX ForwardDistributionGGX
#define GeometrySchlickGGX ForwardGeometrySchlickGGX
#define GeometrySmith ForwardGeometrySmith
#define FresnelSchlick ForwardFresnelSchlick
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"
#undef DistributionGGX
#undef GeometrySchlickGGX
#undef GeometrySmith
#undef FresnelSchlick

struct ReferenceArguments {
    float4x4 inverse_view_projection;
    float3 camera_position;
    float receiver_plane_z;
    uint lights_srv;
    uint baseline_srv;
    uint output_uav;
    uint light_count;
    uint normal_srv;
    uint material_srv;
    uint base_color_srv;
    uint forward_shading;
    uint2 extent;
    float pre_exposure;
    uint reserved;
};

cbuffer ReferenceRoot : register(b2, space0) {
    uint unused;
    uint arguments_srv;
};

// Controlled planar receivers, with coverage/common color from a no-light frame.
// This entry point has no cluster, index-list or lighting-publication input.
[numthreads(64, 1, 1)]
void CS(uint3 thread : SV_DispatchThreadID) {
    StructuredBuffer<ReferenceArguments> arguments = ResourceDescriptorHeap[arguments_srv];
    ReferenceArguments args = arguments[0];
    if (thread.x >= args.extent.x * args.extent.y) return;
    StructuredBuffer<float4> baseline_pixels = ResourceDescriptorHeap[args.baseline_srv];
    RWByteAddressBuffer output = ResourceDescriptorHeap[args.output_uav];
    float4 baseline = baseline_pixels[thread.x];
    if (baseline.a == 0.0) {
        output.Store4(thread.x * 16, asuint(baseline));
        return;
    }
    uint2 pixel = uint2(thread.x % args.extent.x, thread.x / args.extent.x);
    float2 uv = (float2(pixel) + 0.5) / float2(args.extent);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 near_h = mul(args.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 mid_h = mul(args.inverse_view_projection, float4(ndc, 0.5, 1.0));
    float3 near_point = near_h.xyz / near_h.w;
    float3 ray = mid_h.xyz / mid_h.w - near_point;
    float3 world = near_point + ray * ((args.receiver_plane_z - near_point.z) / ray.z);
    DeferredLightingSurfaceData surface = (DeferredLightingSurfaceData)0;
    surface.world_position = world;
    surface.view_direction = normalize(args.camera_position - world);
    surface.world_normal = float3(0.0, 0.0, 1.0);
    surface.base_color = 1.0.xxx;
    surface.roughness = 1.0;
    surface.specular = 0.5;
    surface.ambient_occlusion = 1.0;
    if (args.forward_shading == 0) {
        Texture2D<float4> normals = ResourceDescriptorHeap[args.normal_srv];
        Texture2D<float4> materials = ResourceDescriptorHeap[args.material_srv];
        Texture2D<float4> colors = ResourceDescriptorHeap[args.base_color_srv];
        uint model;
        surface.world_normal = VortexSafeNormalize(DecodeGBufferNormal(normals.Load(int3(pixel, 0))));
        DecodeGBufferMaterial(materials.Load(int3(pixel, 0)), surface.metallic,
            surface.specular, surface.roughness, model);
        DecodeGBufferBaseColor(colors.Load(int3(pixel, 0)), surface.base_color,
            surface.ambient_occlusion);
        surface.roughness = max(surface.roughness, kVortexDeferredMinRoughness);
    }
    surface.specular_f0 = ComputeMetallicF0(surface.base_color, surface.metallic, surface.specular);
    StructuredBuffer<ForwardLocalLightRecord> lights = ResourceDescriptorHeap[args.lights_srv];
    float3 result = 0.0.xxx;
    for (uint index = 0; index < args.light_count; ++index) {
        ForwardLocalLightRecord light = lights[index];
        float3 to_light = light.position_ws - world;
        float distance = length(to_light);
        float3 L = to_light / max(distance, 1.0e-6);
        float attenuation = ComputeLocalLightDistanceAttenuation(to_light, light.range_m);
        if (light.kind == FORWARD_LOCAL_LIGHT_SPOT) {
            attenuation *= ComputeSpotLightAngularAttenuation(L, light.emitted_direction_ws,
                light.inner_cone_sin_half_squared, light.outer_cone_sin_half_squared);
        }
        float3 incident = light.intensity_rgb_cd * attenuation;
        if (args.forward_shading != 0) {
            float NoL = saturate(dot(surface.world_normal, L));
            if (NoL > 0.0) {
                float3 H = normalize(surface.view_direction + L);
                result += EvaluateForwardDirectBrdf(
                    saturate(dot(surface.world_normal, surface.view_direction)), NoL,
                    saturate(dot(surface.world_normal, H)), saturate(dot(surface.view_direction, H)),
                    surface.specular_f0, surface.base_color, surface.metallic, surface.roughness)
                    * incident * NoL;
            }
        } else {
            result += EvaluateCookTorranceLighting(surface, L, incident);
        }
    }
    output.Store4(thread.x * 16,
        asuint(float4(baseline.rgb + result * args.pre_exposure * baseline.a, baseline.a)));
}
