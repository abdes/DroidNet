//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
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
    float _pad0;
};

static float3 ACESFitted(float3 color)
{
    static const float3x3 kInputMat = {
        {0.59719, 0.35458, 0.04823},
        {0.07600, 0.90834, 0.01566},
        {0.02840, 0.13383, 0.83777}
    };
    static const float3x3 kOutputMat = {
        { 1.60475, -0.53108, -0.07367},
        {-0.10208,  1.10813, -0.00605},
        {-0.00327, -0.07276,  1.07602}
    };

    color = mul(kInputMat, color);
    const float3 a = color * (color + 0.0245786) - 0.000090537;
    const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    color = mul(kOutputMat, color);
    return saturate(color);
}

static float3 Reinhard(float3 color)
{
    return color / (color + 1.0);
}

static float3 Uncharted2Tonemap(float3 x)
{
    static const float A = 0.15;
    static const float B = 0.50;
    static const float C = 0.10;
    static const float D = 0.20;
    static const float E = 0.02;
    static const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

static float3 Filmic(float3 color)
{
    static const float white_point = 11.2;
    static const float exposure_bias = 2.0;
    const float3 curr = Uncharted2Tonemap(exposure_bias * color);
    const float3 white_scale = 1.0 / Uncharted2Tonemap(white_point);
    return curr * white_scale;
}

static float DitherBayer4x4(uint2 pixel_pos)
{
    static const float kBayer4x4[16] = {
        0.0,  8.0,  2.0, 10.0,
        12.0, 4.0, 14.0,  6.0,
        3.0, 11.0,  1.0,  9.0,
        15.0, 7.0, 13.0,  5.0
    };

    const uint index = (pixel_pos.x & 3u) | ((pixel_pos.y & 3u) << 2u);
    return (kBayer4x4[index] / 16.0) - 0.5;
}

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

    ConstantBuffer<TonemapPassConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass.source_texture_index == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    Texture2D<float4> scene_signal = ResourceDescriptorHeap[pass.source_texture_index];
    uint width = 0u;
    uint height = 0u;
    scene_signal.GetDimensions(width, height);

    const uint2 pixel = min(
        uint2(input.uv * float2(width, height)),
        uint2(max(width, 1u) - 1u, max(height, 1u) - 1u));

    float3 color = scene_signal.Load(int3(pixel, 0)).rgb;

    if (pass.bloom_texture_index != K_INVALID_BINDLESS_INDEX && pass.bloom_intensity > 0.0f) {
        Texture2D<float4> bloom_texture = ResourceDescriptorHeap[pass.bloom_texture_index];
        color += bloom_texture.Load(int3(pixel, 0)).rgb * pass.bloom_intensity;
    }

    float exposure = max(pass.exposure, 0.0f);
    if (pass.exposure_buffer_index != K_INVALID_BINDLESS_INDEX) {
        ByteAddressBuffer exposure_buffer = ResourceDescriptorHeap[pass.exposure_buffer_index];
        exposure = max(asfloat(exposure_buffer.Load(4)), 0.0f);
    }
    color *= exposure;

    switch (pass.tone_mapper) {
        case 1u:
            color = ACESFitted(color);
            break;
        case 2u:
            color = Filmic(color);
            break;
        case 3u:
            color = Reinhard(color);
            break;
        case 0u:
        default:
            color = saturate(color);
            break;
    }

    color = pow(max(color, 0.0f), 1.0f / max(pass.gamma, 1.0e-4f));
    color = saturate(color + (DitherBayer4x4(pixel) / 255.0f));
    return float4(color, 1.0f);
}
