//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Common/Math.hlsli"
#include "Common/Lighting.hlsli"

#define GROUP_SIZE 16
#define HISTOGRAM_BINS 256

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for Auto Exposure Histogram
struct AutoExposureHistogramConstants {
    uint source_texture_index;
    uint histogram_buffer_index;
    float min_log_luminance;
    float inv_log_luminance_range;
    uint screen_width;
    uint screen_height;
    uint metering_mode; // 0=Average, 1=Center-Weighted, 2=Spot
};

groupshared uint s_Histogram[HISTOGRAM_BINS];

[numthreads(256, 1, 1)]
void ClearHistogram(uint groupIndex : SV_GroupIndex)
{
    ConstantBuffer<AutoExposureHistogramConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    RWByteAddressBuffer histogramBuffer = ResourceDescriptorHeap[pass.histogram_buffer_index];
    histogramBuffer.Store(groupIndex * 4, 0);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CS(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    // Initialize shared histogram
    if (groupIndex < HISTOGRAM_BINS) {
        s_Histogram[groupIndex] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    ConstantBuffer<AutoExposureHistogramConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (dispatchThreadId.x < pass.screen_width && dispatchThreadId.y < pass.screen_height) {
        Texture2D<float4> src_tex = ResourceDescriptorHeap[pass.source_texture_index];
        float3 color = src_tex[dispatchThreadId.xy].rgb;
        float lum = Luminance(color);

        if (lum > 0.0001) {
            float logLum = saturate((log2(lum) - pass.min_log_luminance) * pass.inv_log_luminance_range);
            uint bin = uint(logLum * (HISTOGRAM_BINS - 1.0));

            // Metering weights
            float weight = 1.0;
            if (pass.metering_mode == 1) { // Center-Weighted
                float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(pass.screen_width, pass.screen_height);
                float2 dist = (uv - 0.5) * 2.0;
                weight = saturate(1.0 - length(dist));
            } else if (pass.metering_mode == 2) { // Spot
                float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(pass.screen_width, pass.screen_height);
                float2 dist = (uv - 0.5) * 2.0;
                weight = saturate(1.0 - length(dist) / 0.2);
                weight = weight * weight;
            }

            if (weight > 0.01) {
                InterlockedAdd(s_Histogram[bin], uint(weight * 255.0));
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Copy to global buffer
    if (groupIndex < HISTOGRAM_BINS) {
        RWByteAddressBuffer histogramBuffer = ResourceDescriptorHeap[pass.histogram_buffer_index];
        histogramBuffer.InterlockedAdd(groupIndex * 4, s_Histogram[groupIndex]);
    }
}
