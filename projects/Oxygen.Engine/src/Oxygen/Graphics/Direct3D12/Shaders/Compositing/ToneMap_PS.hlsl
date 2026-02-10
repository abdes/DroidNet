//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/GpuDebug.hlsli"

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
    uint exposure_buffer_index;
    uint tone_mapper;  // One of TONEMAPPER_* constants.
    float exposure;
    uint debug_flags;
    float2 _pad;
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
    float exposure = pass.exposure;
    if (pass.exposure_buffer_index != K_INVALID_BINDLESS_INDEX) {
        ByteAddressBuffer exposure_buf
            = ResourceDescriptorHeap[pass.exposure_buffer_index];
        exposure = max(asfloat(exposure_buf.Load(4)), 0.0f);
    }

#if 0
    // Emit a single debug cross per frame from the tonemap shader so we can
    // validate the exposure inputs in-shader. Guarded to one pixel to avoid
    // flooding the GPU debug line buffer.
    if (all(uint2(input.position.xy) == uint2(0u, 0u))) {
        float dbg_avg_lum = 0.0f;
        float dbg_exposure = exposure;
        const bool has_exposure_buffer
            = (pass.exposure_buffer_index != K_INVALID_BINDLESS_INDEX);
        if (has_exposure_buffer) {
            ByteAddressBuffer dbg_buf
                = ResourceDescriptorHeap[pass.exposure_buffer_index];
            dbg_avg_lum = asfloat(dbg_buf.Load(0));
            dbg_exposure = asfloat(dbg_buf.Load(4));
        }

        // Encode what the shader *actually* sees in the pass constants:
        // - R: exposure_buffer_index low 8 bits
        // - G: exposure_buffer_index next 8 bits
        // - B: debug_flags low 8 bits
        // This lets us distinguish "shader sees 23" vs "shader sees 0xFFFFFFFF".
        const uint idx = pass.exposure_buffer_index;
        const uint flags = pass.debug_flags;
        float3 cross_color = float3(
            float(idx & 0xFFu) / 255.0f,
            float((idx >> 8u) & 0xFFu) / 255.0f,
            float(flags & 0xFFu) / 255.0f);

        // Place the cross roughly in the center of the camera frustum.
        // We avoid `inverse()` (not available in this shader compile environment)
        // by extracting a world-space forward vector from the view matrix.
        // Note: Engine conventions treat objects in front as negative view-space Z.
        const float3 forward_vs = float3(0.0f, 0.0f, -1.0f);
        float3 forward_ws = mul(transpose((float3x3)view_matrix), forward_vs);
        forward_ws /= max(length(forward_ws), 1e-5f);

        const float3 cross_pos_ws = camera_position + forward_ws * 2.0f;
        AddGpuDebugCross(cross_pos_ws, cross_color, 0.25f);

        // Debug Cross 2: Average Luminance (Green = 0.18, Red = 0, Blue = Very Bright)
        // Mapped to Log2 space for visualization.
        if (has_exposure_buffer) {
            float log_avg = log2(max(dbg_avg_lum, 0.0001));
            // Map [-10, +10] to [0, 1] roughly.
            float t = saturate((log_avg + 10.0) / 20.0);
            float3 lum_color = float3(t, 1.0 - abs(t - 0.5) * 2.0, 1.0 - t);

            float3 cross2_pos = cross_pos_ws + float3(0.1, 0.0, 0.0); // Offset right
            AddGpuDebugCross(cross2_pos, lum_color, 0.2f);
        }

        // Debug Cross 3: Exposure (Cyan = 1.0, changes intensity)
        if (has_exposure_buffer) {
             float3 exp_color = float3(0.0, dbg_exposure * 0.1, dbg_exposure * 0.1);
             // If exposure is massive (white wash), this will be bright cyan/white.
             // If exposure is tiny, this will be black.

             float3 cross3_pos = cross_pos_ws - float3(0.1, 0.0, 0.0); // Offset left
             AddGpuDebugCross(cross3_pos, exp_color, 0.2f);
        }

        // Debug Cross 4: Histogram Count (Blue = Full, Black = Zero)
        if (has_exposure_buffer) {
             ByteAddressBuffer dbg_buf = ResourceDescriptorHeap[pass.exposure_buffer_index];
             uint count = dbg_buf.Load(12);

             // Normalize based on 1080p roughly (2 million pixels)
             float t = saturate(float(count) / (1920.0 * 1080.0));
             float3 count_color = float3(0.0, 0.0, max(t, 0.2));

             if (count == 0) count_color = float3(1.0, 0.0, 0.0); // RED ALERT for 0 count.

             float3 cross4_pos = cross_pos_ws - float3(0.2, 0.0, 0.0); // Offset far left
             AddGpuDebugCross(cross4_pos, count_color, 0.2f);
        }

        // Debug Cross 4: Histogram Count (Blue = Full, Black = Zero)
        if (has_exposure_buffer) {
             ByteAddressBuffer dbg_buf = ResourceDescriptorHeap[pass.exposure_buffer_index];
             uint count = dbg_buf.Load(12);

             // Normalize based on 1080p roughly (2 million pixels)
             float t = saturate(float(count) / (1920.0 * 1080.0));
             float3 count_color = float3(0.0, 0.0, max(t, 0.2));
             // If count is 0, it will be Dark Blue (0.2). If full, Bright Blue.
             // If extremely low but non-zero, it will be 0.2.
             // Only if TRULY 0, we can't tell difference from small count with this max(t, 0.2).
             // Let's use Red for 0.
             if (count == 0) count_color = float3(1.0, 0.0, 0.0); // RED ALERT for 0 count.

             float3 cross4_pos = cross_pos_ws - float3(0.2, 0.0, 0.0); // Offset far left
             AddGpuDebugCross(cross4_pos, count_color, 0.2f);
        }
    }
#endif

    color *= exposure;

    // Apply tonemapping based on mode
    switch (pass.tone_mapper) {
        case TONEMAPPER_ACES_FITTED:
            color = ACESFitted(color);
            break;
        case TONEMAPPER_REINHARD:
            color = Reinhard(color);
            break;
        case TONEMAPPER_NONE:
            // Just clamp to [0,1] for display
            color = saturate(color);
            break;
        case TONEMAPPER_FILMIC:
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
