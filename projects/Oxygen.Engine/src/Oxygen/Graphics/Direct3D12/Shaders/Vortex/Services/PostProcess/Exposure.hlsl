//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#define GROUP_SIZE 16
#define HISTOGRAM_BINS 256

static const float K_MIDDLE_GREY = 0.18f;

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AutoExposureHistogramConstants {
    uint source_texture_index;
    uint histogram_buffer_index;
    float min_log_luminance;
    float inv_log_luminance_range;
    uint metering_left;
    uint metering_top;
    uint metering_width;
    uint metering_height;
    uint metering_mode;
    float spot_meter_radius;
    uint _pad0;
    uint _pad1;
};

struct AutoExposureAverageConstants {
    uint histogram_buffer_index;
    uint exposure_buffer_index;
    float min_log_luminance;
    float log_luminance_range;
    float low_percentile;
    float high_percentile;
    float min_ev;
    float max_ev;
    float adaptation_speed_up;
    float adaptation_speed_down;
    float delta_time;
    float target_luminance;
};

groupshared uint s_Histogram[HISTOGRAM_BINS];

static float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(256, 1, 1)]
void ClearHistogram(uint group_index : SV_GroupIndex)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    ConstantBuffer<AutoExposureHistogramConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass.histogram_buffer_index == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    RWByteAddressBuffer histogram_buffer
        = ResourceDescriptorHeap[pass.histogram_buffer_index];
    histogram_buffer.Store(group_index * 4, 0);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void VortexExposureHistogramCS(
    uint3 dispatch_thread_id : SV_DispatchThreadID,
    uint group_index : SV_GroupIndex)
{
    if (group_index < HISTOGRAM_BINS) {
        s_Histogram[group_index] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    ConstantBuffer<AutoExposureHistogramConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass.source_texture_index == K_INVALID_BINDLESS_INDEX
        || pass.histogram_buffer_index == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    if (dispatch_thread_id.x < pass.metering_width
        && dispatch_thread_id.y < pass.metering_height) {
        Texture2D<float4> source_texture
            = ResourceDescriptorHeap[pass.source_texture_index];
        const uint2 sample_coord = uint2(pass.metering_left, pass.metering_top)
            + dispatch_thread_id.xy;
        const float3 color = source_texture[sample_coord].rgb;
        const float luminance = max(Luminance(color), 1.0e-6);
        const float log_luminance = saturate(
            (log2(luminance) - pass.min_log_luminance)
            * pass.inv_log_luminance_range);
        const uint bin = uint(log_luminance * (HISTOGRAM_BINS - 1.0));

        float weight = 1.0;
        if (pass.metering_mode == 1u) {
            const float2 uv = (float2(dispatch_thread_id.xy) + 0.5)
                / float2(pass.metering_width, pass.metering_height);
            const float2 dist = (uv - 0.5) * 2.0;
            weight = saturate(1.0 - length(dist));
        } else if (pass.metering_mode == 2u) {
            const float2 uv = (float2(dispatch_thread_id.xy) + 0.5)
                / float2(pass.metering_width, pass.metering_height);
            const float2 dist = (uv - 0.5) * 2.0;
            const float radius = max(pass.spot_meter_radius, 1.0e-4);
            weight = saturate(1.0 - length(dist) / radius);
            weight *= weight;
        }

        if (weight > 0.0) {
            InterlockedAdd(s_Histogram[bin], uint(weight * 255.0 + 0.5));
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (group_index < HISTOGRAM_BINS) {
        RWByteAddressBuffer histogram_buffer
            = ResourceDescriptorHeap[pass.histogram_buffer_index];
        histogram_buffer.InterlockedAdd(group_index * 4, s_Histogram[group_index]);
    }
}

[numthreads(1, 1, 1)]
void VortexExposureAverageCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    (void)dispatch_thread_id;
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    ConstantBuffer<AutoExposureAverageConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass.histogram_buffer_index == K_INVALID_BINDLESS_INDEX
        || pass.exposure_buffer_index == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    RWByteAddressBuffer histogram_buffer
        = ResourceDescriptorHeap[pass.histogram_buffer_index];
    RWByteAddressBuffer exposure_buffer
        = ResourceDescriptorHeap[pass.exposure_buffer_index];

    float previous_luminance = asfloat(exposure_buffer.Load(0));
    if (previous_luminance <= 0.0) {
        previous_luminance = max(pass.target_luminance, 0.0001);
    }
    previous_luminance = max(previous_luminance, 1.0e-6);
    const float previous_log_luminance = log2(previous_luminance);

    uint count = 0;
    [unroll]
    for (uint index = 0; index < HISTOGRAM_BINS; ++index) {
        count += histogram_buffer.Load(index * 4);
    }

    float target_log_luminance = previous_log_luminance;
    if (count > 0) {
        float low_bound = saturate(pass.low_percentile) * float(count);
        float high_bound = saturate(pass.high_percentile) * float(count);
        high_bound = max(high_bound, low_bound);

        uint cumulative_count = 0;
        float valid_weight = 0.0;
        float weighted_sum = 0.0;

        [loop]
        for (uint index = 0; index < HISTOGRAM_BINS; ++index) {
            const uint bin_value = histogram_buffer.Load(index * 4);
            const uint bin_start = cumulative_count;
            const uint bin_end = cumulative_count + bin_value;
            cumulative_count += bin_value;

            const float overlap_start = max(float(bin_start), low_bound);
            const float overlap_end = min(float(bin_end), high_bound);
            const float overlap_count = max(0.0, overlap_end - overlap_start);
            if (overlap_count > 0.0) {
                const float log_luminance = pass.min_log_luminance
                    + (float(index) / float(HISTOGRAM_BINS - 1))
                        * pass.log_luminance_range;
                weighted_sum += log_luminance * overlap_count;
                valid_weight += overlap_count;
            }
        }

        if (valid_weight > 0.0) {
            target_log_luminance = weighted_sum / valid_weight;
        }
    }

    const float diff = target_log_luminance - previous_log_luminance;
    const float speed = diff > 0.0
        ? max(pass.adaptation_speed_up, 0.0)
        : max(pass.adaptation_speed_down, 0.0);
    const float interpolation = saturate(1.0 - exp(-max(pass.delta_time, 0.0) * speed));
    const float smoothed_log_luminance = lerp(
        previous_log_luminance, target_log_luminance, interpolation);
    const float min_average_luminance = K_MIDDLE_GREY * exp2(pass.min_ev);
    const float max_average_luminance = K_MIDDLE_GREY * exp2(pass.max_ev);
    const float average_luminance = clamp(
        max(exp2(smoothed_log_luminance), 0.0001),
        min_average_luminance,
        max(max_average_luminance, min_average_luminance));

    float exposure = pass.target_luminance / average_luminance;
    exposure = clamp(exposure, 1.0e-8, 64000.0);
    if (exposure != exposure || abs(exposure) > 1.0e6) {
        exposure = 1.0;
    }

    const float ev = log2(max(1.0e-4, average_luminance / K_MIDDLE_GREY));
    exposure_buffer.Store(0, asuint(average_luminance));
    exposure_buffer.Store(4, asuint(exposure));
    exposure_buffer.Store(8, asuint(ev));
    exposure_buffer.Store(12, count);
}
