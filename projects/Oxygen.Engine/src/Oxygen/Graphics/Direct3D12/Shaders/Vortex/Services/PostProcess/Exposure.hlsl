//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"

#define GROUP_SIZE 16
#define HISTOGRAM_BINS 256
#define HISTOGRAM_WORDS 264
#define METER_GRID_LIMIT 512

// Counters count samples, not quantized mass. Last two words are reserved.
static const uint METER_FINITE = 256u;
static const uint METER_WEIGHTED = 257u;
static const uint METER_BLACK = 258u;
static const uint METER_BELOW_WINDOW = 259u;
static const uint METER_REJECTED = 260u;
static const uint METER_DARK = 261u;

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
    uint mask_texture_index;
    uint background_enabled;
    float one_over_pre_exposure;
    float black_influence;
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
    float log2_transition_distance;
    float log2_speed_up;
    float log2_speed_down;
    float log2_delta_time;
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

groupshared uint s_Histogram[HISTOGRAM_WORDS];

static float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

static uint QuantizeWeight(float weight)
{
    // Explicit half-up rule, independent of round()'s ties-to-even behavior.
    return uint(floor(saturate(weight) * 4095.0 + 0.5));
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
    for (uint index = group_index; index < HISTOGRAM_WORDS; index += 256u) {
        histogram_buffer.Store(index * 4u, 0u);
    }
}

static void MeterSample(AutoExposureHistogramConstants pass, uint2 cell)
{
    const uint2 grid = min(uint2(pass.metering_width, pass.metering_height), METER_GRID_LIMIT);
    if (any(cell >= grid)) {
        return;
    }
    const float2 uv = (float2(cell) + 0.5) / float2(grid);
    const uint2 coord = uint2(pass.metering_left, pass.metering_top)
        + min(uint2(uv * float2(pass.metering_width, pass.metering_height)),
              uint2(pass.metering_width, pass.metering_height) - 1u);
    Texture2D<float4> source = ResourceDescriptorHeap[pass.source_texture_index];
    const float4 sample = source.Load(int3(coord, 0));
    float mask = 1.0;
    if (pass.mask_texture_index != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float4> mask_texture = ResourceDescriptorHeap[pass.mask_texture_index];
        SamplerState mask_sampler = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
        mask = mask_texture.SampleLevel(mask_sampler, uv, 0).r;
    }
    if (!all(isfinite(sample.rgb)) || !isfinite(mask)
        || (pass.background_enabled != 0u && !isfinite(sample.a))) {
        InterlockedAdd(s_Histogram[METER_REJECTED], 1u);
        return;
    }
    const float coverage = pass.background_enabled != 0u ? saturate(sample.a) : 1.0;
    float profile = 1.0;
    const float distance = length((uv - 0.5) * 2.0);
    if (pass.metering_mode == 1u) {
        profile = saturate(1.0 - distance);
    } else if (pass.metering_mode == 2u) {
        profile = pass.spot_meter_radius > 0.0
            ? saturate(1.0 - distance / pass.spot_meter_radius)
            : (all(cell * 2u + 1u == grid) ? 1.0 : 0.0);
        profile *= profile;
    }
    const uint weight = QuantizeWeight(profile * saturate(mask) * coverage);
    if (weight == 0u) {
        InterlockedAdd(s_Histogram[METER_FINITE], 1u);
        return;
    }
    const float3 color = (sample.rgb / coverage) * pass.one_over_pre_exposure;
    const float luminance = Luminance(color);
    if (!all(isfinite(color)) || !isfinite(luminance)) {
        InterlockedAdd(s_Histogram[METER_REJECTED], 1u);
        return;
    }
    InterlockedAdd(s_Histogram[METER_FINITE], 1u);
    InterlockedAdd(s_Histogram[METER_WEIGHTED], 1u);
    const float lower_bound = exp2(pass.min_log_luminance);
    if (luminance <= lower_bound) {
        InterlockedAdd(s_Histogram[METER_DARK], 1u);
        if (luminance == 0.0) {
            InterlockedAdd(s_Histogram[METER_BLACK], 1u);
        } else if (luminance > 0.0) {
            InterlockedAdd(s_Histogram[METER_BELOW_WINDOW], 1u);
        }
        // Apply influence to classified dark samples, never to ordinary mass
        // interpolated into bin zero from above the window boundary.
        const uint dark_weight = uint(floor(float(weight) * pass.black_influence + 0.5));
        InterlockedAdd(s_Histogram[0], dark_weight);
        return;
    }
    const float position = saturate((log2(luminance) - pass.min_log_luminance)
        * pass.inv_log_luminance_range) * 255.0;
    const uint lower = uint(position);
    const uint upper_weight = uint(floor(float(weight) * frac(position) + 0.5));
    InterlockedAdd(s_Histogram[lower], weight - upper_weight);
    InterlockedAdd(s_Histogram[min(lower + 1u, 255u)], upper_weight);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void VortexExposureHistogramCS(
    uint3 dispatch_thread_id : SV_DispatchThreadID,
    uint group_index : SV_GroupIndex)
{
    for (uint index = group_index; index < HISTOGRAM_WORDS; index += 256u) {
        s_Histogram[index] = 0u;
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
    MeterSample(pass, dispatch_thread_id.xy);
    GroupMemoryBarrierWithGroupSync();
    RWByteAddressBuffer histogram = ResourceDescriptorHeap[pass.histogram_buffer_index];
    for (uint index = group_index; index < HISTOGRAM_WORDS; index += 256u) {
        histogram.InterlockedAdd(index * 4u, s_Histogram[index]);
    }
}

// Exact integer part of float32-percentile * uint mass, plus a rational
// remainder. No float64 device requirement and no large float CDF subtraction.
struct PercentileBoundary {
    uint whole;
    uint64_t remainder;
    uint shift;
};

static PercentileBoundary MakeBoundary(float percentile, uint mass)
{
    const uint bits = asuint(percentile);
    const uint exponent = (bits >> 23u) & 255u;
    const uint mantissa = (bits & 0x7fffffu) | (exponent != 0u ? 0x800000u : 0u);
    const uint shift = exponent != 0u ? 150u - exponent : 149u;
    const uint64_t product = uint64_t(mantissa) * uint64_t(mass);
    PercentileBoundary result;
    result.shift = shift;
    result.whole = shift < 64u ? uint(product >> shift) : 0u;
    result.remainder = shift < 64u ? product & ((uint64_t(1) << shift) - 1) : product;
    return result;
}

static float BoundaryFraction(PercentileBoundary boundary)
{
    // Tiny intervals wholly within the first mass unit are handled exactly by
    // the containing-bin branch. This underflow cannot affect a boundary split.
    return boundary.shift > 120u ? 0.0
        : float(boundary.remainder) * exp2(-float(boundary.shift));
}

static float BoundaryTail(PercentileBoundary boundary)
{
    if (boundary.shift >= 64u) {
        return 1.0 - BoundaryFraction(boundary);
    }
    return float((uint64_t(1) << boundary.shift) - boundary.remainder)
        * exp2(-float(boundary.shift));
}

static bool MeterHistogram(RWByteAddressBuffer histogram,
    AutoExposureAverageConstants pass, out float log_luminance)
{
    log_luminance = 0.0;
    uint total = 0u;
    for (uint i = 0u; i < HISTOGRAM_BINS; ++i) {
        total += histogram.Load(i * 4u);
    }
    if (total == 0u) {
        return false;
    }
    const PercentileBoundary low = MakeBoundary(pass.low_percentile, total);
    const PercentileBoundary high = MakeBoundary(pass.high_percentile, total);
    uint cursor = 0u;
    float weight = 0.0;
    float moment = 0.0;
    for (uint index = 0u; index < HISTOGRAM_BINS; ++index) {
        const uint end = cursor + histogram.Load(index * 4u);
        const float value = pass.min_log_luminance + float(index) * (pass.log_luminance_range / 255.0);
        if (low.whole == high.whole && cursor <= low.whole && low.whole < end) {
            log_luminance = value;
            return true;
        }
        const uint integer_start = max(cursor, low.whole + (low.remainder != 0 ? 1u : 0u));
        const uint integer_end = min(end, high.whole);
        float overlap = float(integer_end > integer_start ? integer_end - integer_start : 0u);
        if (low.remainder != 0 && cursor <= low.whole && low.whole < end) {
            overlap += BoundaryTail(low);
        }
        if (high.remainder != 0 && cursor <= high.whole && high.whole < end) {
            overlap += BoundaryFraction(high);
        }
        weight += overlap;
        moment += overlap * value;
        cursor = end;
    }
    if (weight <= 0.0) {
        return false;
    }
    log_luminance = moment / weight;
    return true;
}

static float AdaptLogGain(float previous, float target, float log_speed,
    float log_delta_time, float log_distance)
{
    const float difference = target - previous;
    const float radius = abs(difference);
    if (radius == 0.0 || log_delta_time == -256.0 || log_speed == -256.0) {
        return previous;
    }
    const float log_travel = log_speed + log_delta_time;
    // Radius cannot exceed 64 stops. Distance outside its range need not be
    // reconstructed; subnormal distances are below float32 log-gain precision.
    const float distance = log_distance >= 6.0 ? 64.0
        : log_distance < -126.0 ? 0.0 : exp2(log_distance);
    const float linear_distance = max(radius - distance, 0.0);
    float log_tail_travel = log_travel;
    if (linear_distance > 0.0) {
        if (log_travel <= log2(linear_distance)) {
            const float travel = log_travel < -126.0 ? 0.0 : exp2(log_travel);
            return previous + sign(difference) * min(travel, linear_distance);
        }
        // Only reconstruct travel when bounded. Otherwise the remaining
        // fraction is >= 1/2, so subtraction is well conditioned.
        if (log_travel <= 7.0) {
            const float tail = exp2(log_travel) - linear_distance;
            if (tail <= 0.0) {
                return target - sign(difference) * distance;
            }
            log_tail_travel = log2(tail);
        } else {
            log_tail_travel += log2(1.0 - exp2(log2(linear_distance) - log_travel));
        }
    }
    const float log_exponent = log_tail_travel - log_distance;
    // exp(-128) * 64 stops is below the representable gain-step precision.
    if (log_exponent >= 7.0 || distance == 0.0) {
        return target;
    }
    const float exponent = log_exponent < -126.0 ? 0.0 : exp2(log_exponent);
    const float remaining = min(radius, distance) * exp(-exponent);
    return target - sign(difference) * remaining;
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

    const uint weighted_count = histogram_buffer.Load(METER_WEIGHTED * 4u);
    const bool dark = weighted_count > 0u
        && weighted_count == histogram_buffer.Load(METER_DARK * 4u);
    float target_log_luminance = 0.0;
    const bool valid_meter = dark || MeterHistogram(histogram_buffer, pass, target_log_luminance);
    if (dark) {
        target_log_luminance = pass.min_log_luminance;
    }

    const bool locked = (targets.flags & 1u) != 0u;
    const bool zero_target = (targets.flags & 2u) != 0u;
    const bool restoring_positive = !zero_target && (previous_flags & EXPOSURE_ZERO_TARGET) != 0u;
    const bool has_meter_history = (previous_flags & EXPOSURE_HAS_METER_HISTORY) != 0u;
    const float previous_ev = asfloat(exposure_buffer.Load(EXPOSURE_METER_EV_OFFSET));
    const bool previous_dark = (previous_flags & EXPOSURE_SYNTHETIC_DARK) != 0u;
    const float ev = dark ? pass.min_ev : target_log_luminance - log2(K_MIDDLE_GREY);
    float log_target = previous_log_gain;
    if (locked) {
        log_target = targets.keys[0].y;
    } else if (valid_meter) {
        log_target = dark ? targets.dark_log_gain : ResolveLogTarget(targets, ev);
    } else if (restoring_positive) {
        log_target = has_meter_history
            ? (previous_dark ? targets.dark_log_gain : ResolveLogTarget(targets, previous_ev))
            : targets.initial_log_gain;
    }
    const bool solve = valid_meter || locked || restoring_positive;
    const bool immediate = locked || restoring_positive
        || (previous_flags & EXPOSURE_INITIALIZED) == 0u;
    const float speed = log_target < previous_log_gain
        ? pass.log2_speed_up : pass.log2_speed_down;
    const float log_gain = !solve ? previous_log_gain : immediate ? log_target
        : AdaptLogGain(previous_log_gain, log_target, speed, pass.log2_delta_time, pass.log2_transition_distance);
    const float latent_gain = exp2(log_gain);
    const float positive_target = solve ? exp2(log_target)
        : asfloat(exposure_buffer.Load(12u));
    exposure_buffer.Store4(0u, asuint(float4(zero_target ? 0.0 : latent_gain,
        zero_target ? 0.0 : positive_target, latent_gain, positive_target)));
    if (valid_meter) {
        exposure_buffer.Store2(16u, asuint(float2(exp2(target_log_luminance), ev)));
    }
    uint flags = previous_flags & ~(EXPOSURE_LUMINANCE_VALID | EXPOSURE_METER_EV_VALID | EXPOSURE_ZERO_TARGET);
    if (valid_meter) {
        flags &= ~EXPOSURE_SYNTHETIC_DARK;
        flags |= EXPOSURE_HAS_METER_HISTORY | EXPOSURE_METER_EV_VALID
            | (dark ? EXPOSURE_SYNTHETIC_DARK : EXPOSURE_LUMINANCE_VALID);
    }
    if (valid_meter || locked) {
        flags |= EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED;
    }
    flags |= zero_target ? EXPOSURE_ZERO_TARGET : 0u;
    exposure_buffer.Store(EXPOSURE_FLAGS_OFFSET, flags);
    exposure_buffer.Store(28u, valid_meter || locked ? 0u : 2u);
    exposure_buffer.Store2(32u, pass.settings_revision);
    exposure_buffer.Store2(56u, pass.frame_sequence);
}
