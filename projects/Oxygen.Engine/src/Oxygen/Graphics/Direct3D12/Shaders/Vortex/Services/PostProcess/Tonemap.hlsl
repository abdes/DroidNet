//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Shared/FailedViewPresentation.hlsli"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"
#include "Vortex/Services/PostProcess/ToneMapping.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct TonemapPassConstants
{
    uint source_texture_index;
    uint exposure_buffer_index;
    uint bloom_texture_index;
    uint tone_mapper;
    float exposure;
    float gamma;
    float bloom_intensity;
    uint frame_exposure_srv;
    float3 background_color;
    uint background_enabled;
    uint fallback_texture_index;
    uint conversion_report_index;
    uint lighting_frame_slot;
    uint reserved;
};

[shader("vertex")]
VortexFullscreenTriangleOutput VortexTonemapVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexTonemapPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<TonemapPassConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const TonemapPassConstants pass = pass_buffer[0];
    if (pass.lighting_frame_slot != K_INVALID_BINDLESS_INDEX
        && !IsLightingPublicationReady(LoadLightingFrameBindings(pass.lighting_frame_slot))) {
        return FailedViewColor(input.position.xy);
    }
    if (pass.source_texture_index == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    uint source_index = pass.source_texture_index;
    if (pass.conversion_report_index != K_INVALID_BINDLESS_INDEX) {
        ByteAddressBuffer report = ResourceDescriptorHeap[pass.conversion_report_index];
        if (!IsCheckedSceneColorAccepted(report)) source_index = pass.fallback_texture_index;
    }
    Texture2D<float4> scene_signal = ResourceDescriptorHeap[source_index];
    uint width = 0u;
    uint height = 0u;
    scene_signal.GetDimensions(width, height);

    const uint2 pixel = min(
        uint2(input.uv * float2(width, height)),
        uint2(max(width, 1u) - 1u, max(height, 1u) - 1u));

    const float4 scene = scene_signal.Load(int3(pixel, 0));
    const float coverage = pass.background_enabled != 0u ? saturate(scene.a) : 1.0f;
    const float3 foreground = scene.rgb / max(coverage, 1.0e-6f);
    float3 bloom = 0.0f.xxx;

    if (pass.bloom_texture_index != K_INVALID_BINDLESS_INDEX && pass.bloom_intensity > 0.0f) {
        Texture2D<float4> bloom_texture = ResourceDescriptorHeap[pass.bloom_texture_index];
        bloom = bloom_texture.Load(int3(pixel, 0)).rgb * pass.bloom_intensity;
    }

    float exposure = max(pass.exposure, 0.0f);
    if (pass.exposure_buffer_index != K_INVALID_BINDLESS_INDEX) {
        ByteAddressBuffer exposure_buffer = ResourceDescriptorHeap[pass.exposure_buffer_index];
        exposure = max(asfloat(exposure_buffer.Load(EXPOSURE_DISPLAYED_SCALE_OFFSET)), 0.0f);
    }
    if (pass.frame_exposure_srv != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<FrameExposureData> frame = ResourceDescriptorHeap[pass.frame_exposure_srv];
        exposure *= frame[0].one_over_pre_exposure;
    }
    float3 color = MapForeground((foreground + bloom) * exposure, pass.tone_mapper, pass.gamma);
    if (pass.background_enabled != 0u) {
        const float3 base = MapForeground(foreground * exposure, pass.tone_mapper, pass.gamma);
        // Composite coverage in display-linear space. Bloom remains additive,
        // including its halo outside foreground geometry.
        const float3 composed = SrgbToLinear(color)
            + (saturate(pass.background_color) - SrgbToLinear(base)) * (1.0f - coverage);
        color = LinearToSrgb(composed);
    }
    color = saturate(color + (DitherBayer4x4(pixel) / 255.0f));
    return float4(color, 1.0f);
}
