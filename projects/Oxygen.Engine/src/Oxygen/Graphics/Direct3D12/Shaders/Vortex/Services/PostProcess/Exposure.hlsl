//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"

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
    uint targets_srv;
    uint2 settings_revision;
    uint2 frame_sequence;
};

// CPU mirror: Vortex/Types/ExposureTargetData.h (560 bytes).
struct ExposureTargetData {
    uint key_count;
    uint flags;
    float initial_log_gain;
    float dark_log_gain;
    float2 keys[68];
};

static float ResolveLogTarget(ExposureTargetData targets, float raw_ev)
{
    if (raw_ev <= targets.keys[0].x) {
        return targets.keys[0].y;
    }
    [loop]
    for (uint i = 1u; i < targets.key_count; ++i) {
        if (raw_ev <= targets.keys[i].x) {
            float2 left = targets.keys[i - 1u];
            float2 right = targets.keys[i];
            return lerp(left.y, right.y, (raw_ev - left.x) / (right.x - left.x));
        }
    }
    return targets.keys[targets.key_count - 1u].y;
}

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

    StructuredBuffer<AutoExposureHistogramConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AutoExposureHistogramConstants pass = pass_buffer[0];
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

    StructuredBuffer<AutoExposureHistogramConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AutoExposureHistogramConstants pass = pass_buffer[0];
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

    StructuredBuffer<AutoExposureAverageConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AutoExposureAverageConstants pass = pass_buffer[0];
    if (pass.histogram_buffer_index == K_INVALID_BINDLESS_INDEX
        || pass.exposure_buffer_index == K_INVALID_BINDLESS_INDEX
        || pass.targets_srv == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    RWByteAddressBuffer histogram_buffer
        = ResourceDescriptorHeap[pass.histogram_buffer_index];
    RWByteAddressBuffer exposure_buffer
        = ResourceDescriptorHeap[pass.exposure_buffer_index];

    StructuredBuffer<ExposureTargetData> target_buffer = ResourceDescriptorHeap[pass.targets_srv];
    const ExposureTargetData targets = target_buffer[0];
    if (targets.key_count == 0u || targets.key_count > 68u) {
        return;
    }
    float previous_latent_gain = asfloat(exposure_buffer.Load(EXPOSURE_LATENT_SCALE_OFFSET));
    if (!isfinite(previous_latent_gain) || previous_latent_gain <= 0.0) {
        previous_latent_gain = exp2(targets.initial_log_gain);
    }
    const float previous_log_gain = log2(previous_latent_gain);
    const uint previous_flags = exposure_buffer.Load(EXPOSURE_FLAGS_OFFSET);

    uint count = 0;
    [unroll]
    for (uint index = 0; index < HISTOGRAM_BINS; ++index) {
        count += histogram_buffer.Load(index * 4);
    }

    float target_log_luminance = asfloat(exposure_buffer.Load(EXPOSURE_METER_EV_OFFSET)) + log2(K_MIDDLE_GREY);
    bool valid_meter = false;
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
            valid_meter = true;
        }
    }

    const bool locked = (targets.flags & 1u) != 0u;
    if (!valid_meter && !locked) {
        exposure_buffer.Store(EXPOSURE_FLAGS_OFFSET,
            previous_flags & ~(EXPOSURE_LUMINANCE_VALID | EXPOSURE_METER_EV_VALID));
        exposure_buffer.Store(28u, 2u);
        exposure_buffer.Store2(32u, pass.settings_revision);
        exposure_buffer.Store2(56u, pass.frame_sequence);
        return;
    }
    const float ev = target_log_luminance - log2(K_MIDDLE_GREY);
    const float log_target = locked ? targets.keys[0].y : ResolveLogTarget(targets, ev);
    if (!isfinite(log_target) || log_target < -32.0 || log_target > 32.0) {
        return;
    }
    const float diff = log_target - previous_log_gain;
    const float speed = diff < 0.0
        ? max(pass.adaptation_speed_up, 0.0)
        : max(pass.adaptation_speed_down, 0.0);
    const float interpolation = saturate(1.0 - exp(-max(pass.delta_time, 0.0) * speed));
    const float log_gain = locked || (previous_flags & EXPOSURE_INITIALIZED) == 0u ? log_target
        : lerp(previous_log_gain, log_target, interpolation);
    const float latent_gain = exp2(log_gain);
    const float displayed_gain = (targets.flags & 2u) != 0u ? 0.0 : latent_gain;
    const float positive_target = exp2(log_target);
    exposure_buffer.Store4(0u, asuint(float4(displayed_gain,
        (targets.flags & 2u) != 0u ? 0.0 : positive_target,
        latent_gain, positive_target)));
    if (valid_meter) {
        exposure_buffer.Store2(16u, asuint(float2(exp2(target_log_luminance), ev)));
    }
    exposure_buffer.Store(EXPOSURE_FLAGS_OFFSET,
        EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED
        | (valid_meter ? EXPOSURE_LUMINANCE_VALID | EXPOSURE_METER_EV_VALID : 0u)
        | ((targets.flags & 2u) != 0u ? EXPOSURE_ZERO_TARGET : 0u));
    exposure_buffer.Store(28u, 0u);
    exposure_buffer.Store2(32u, pass.settings_revision);
    exposure_buffer.Store2(56u, pass.frame_sequence);
}
