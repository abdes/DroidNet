//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct ToneMapVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for tonemapping.
// Layout must match C++ ToneMapPassConstants.
struct ToneMapPassConstants {
    uint source_texture_index;
    uint sampler_index;
    float exposure;
    uint tone_mapper;  // 0=ACES, 1=Reinhard, 2=None, 3=Filmic
};

//-----------------------------------------------------------------------------
// Tonemapping Operators
//-----------------------------------------------------------------------------

// ACES Fitted curve (Stephen Hill's approximation)
float3 ACESFitted(float3 color)
{
    // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
    static const float3x3 ACESInputMat = {
        {0.59719, 0.35458, 0.04823},
        {0.07600, 0.90834, 0.01566},
        {0.02840, 0.13383, 0.83777}
    };

    // ODT_SAT => XYZ => D60_2_D65 => sRGB
    static const float3x3 ACESOutputMat = {
        { 1.60475, -0.53108, -0.07367},
        {-0.10208,  1.10813, -0.00605},
        {-0.00327, -0.07276,  1.07602}
    };

    color = mul(ACESInputMat, color);

    // RRT and ODT fit
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;

    color = mul(ACESOutputMat, color);

    return saturate(color);
}

// Simple Reinhard operator
float3 Reinhard(float3 color)
{
    return color / (color + 1.0);
}

// Filmic curve (John Hable's Uncharted 2 tonemap)
float3 Uncharted2Tonemap(float3 x)
{
    static const float A = 0.15;  // Shoulder Strength
    static const float B = 0.50;  // Linear Strength
    static const float C = 0.10;  // Linear Angle
    static const float D = 0.20;  // Toe Strength
    static const float E = 0.02;  // Toe Numerator
    static const float F = 0.30;  // Toe Denominator
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 Filmic(float3 color)
{
    static const float W = 11.2;  // Linear White Point
    static const float ExposureBias = 2.0;
    float3 curr = Uncharted2Tonemap(ExposureBias * color);
    float3 whiteScale = 1.0 / Uncharted2Tonemap(W);
    return curr * whiteScale;
}

// 4x4 Bayer ordered dithering. Returns an offset in [-0.5, 0.5].
static inline float DitherBayer4x4(uint2 pixel_pos)
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

//-----------------------------------------------------------------------------
// Main Pixel Shader
//-----------------------------------------------------------------------------

float4 PS(ToneMapVSOutput input) : SV_TARGET
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    ConstantBuffer<ToneMapPassConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (pass.source_texture_index == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint sampler_index = pass.sampler_index != K_INVALID_BINDLESS_INDEX
        ? pass.sampler_index
        : 0u;

    Texture2D<float4> src_tex
        = ResourceDescriptorHeap[pass.source_texture_index];
    SamplerState samp = SamplerDescriptorHeap[sampler_index];

    // Sample HDR color
    float4 hdr_color = src_tex.Sample(samp, input.uv);
    float3 color = hdr_color.rgb;

    // Apply exposure
    color *= pass.exposure;

    // Apply tonemapping based on mode
    switch (pass.tone_mapper) {
        case 0: // ACES Fitted
            color = ACESFitted(color);
            break;
        case 1: // Reinhard
            color = Reinhard(color);
            break;
        case 2: // None (passthrough)
            // Just clamp to [0,1] for display
            color = saturate(color);
            break;
        case 3: // Filmic
            color = Filmic(color);
            break;
        default:
            color = saturate(color);
            break;
    }

    // TEMPORARY WORKAROUND: Dither just before output quantization
    // (e.g. RGBA8_UNORM swapchain) to reduce visible sky/ground banding.
    // TODO(#taa): Remove once we have a higher quality TAA/temporal resolve
    // that provides stable banding suppression.
    const uint2 pixel_pos = uint2(input.position.xy);
    color = saturate(color + (DitherBayer4x4(pixel_pos) / 255.0));

    return float4(color, 1.0);
}
