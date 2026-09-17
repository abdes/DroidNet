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
    uint frame_exposure_srv;
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
    uint previous_state_srv;
    float fixed_scale;
    uint exposure_mode;
    uint control_flags;
    uint2 requested_generation;
    uint transition_policy;
    float seed_log_gain;
    uint status_uav;
    uint borrowed_state_srv;
    uint2 view_lifetime;
};

// CPU mirror: Vortex/Types/ExposureTargetData.h (560 bytes).
struct ExposureTargetData {
    uint key_count;
    uint flags;
    float initial_log_gain;
    float dark_log_gain;
    float2 keys[68];
};

// All unlocked target abscissas are within the supported metered-EV domain.
// Scaling them by 2^64 keeps every possible binary32 spacing normal, without
// overflowing the domain endpoints. Reconstruct subnormals by bits first:
// multiplying a subnormal float directly could flush it before scaling.
static float ScaledMeterEv(float ev)
{
    const uint bits = asuint(ev);
    if ((bits & 0x7f800000u) == 0u) {
        const float magnitude = float(bits & 0x7fffffu) * 2.5849394142282115e-26f; // 2^-85
        return (bits & 0x80000000u) != 0u ? -magnitude : magnitude;
    }
    return ev * 18446744073709551616.0f; // 2^64
}

static float ResolveLogTarget(ExposureTargetData targets, float raw_ev)
{
    const float coordinate = ScaledMeterEv(raw_ev);
    if (coordinate <= ScaledMeterEv(targets.keys[0].x)) {
        return targets.keys[0].y;
    }
    [loop]
    for (uint i = 1u; i < targets.key_count; ++i) {
        const float right_ev = ScaledMeterEv(targets.keys[i].x);
        if (coordinate <= right_ev) {
            const float2 left = targets.keys[i - 1u];
            const float left_ev = ScaledMeterEv(left.x);
            return lerp(left.y, targets.keys[i].y,
                (coordinate - left_ev) / (right_ev - left_ev));
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

static bool IsDarkMeterSample(float luminance, float min_log_luminance)
{
    return luminance <= exp2(min_log_luminance);
}

static uint MeterContribution(uint base_weight, bool dark, float black_influence)
{
    return dark ? uint(floor(float(base_weight) * black_influence + 0.5)) : base_weight;
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
    // Integer offsets keep the exact centre at zero even for tiny positive
    // radii; deriving distance from a rounded UV can move it outside the spot.
    const float2 centered = (float2(cell * 2u + 1u) - float2(grid)) / float2(grid);
    const float distance = length(centered);
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
    float inverse_p = pass.one_over_pre_exposure;
    if (pass.frame_exposure_srv != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<FrameExposureData> frame = ResourceDescriptorHeap[pass.frame_exposure_srv];
        inverse_p = frame[0].one_over_pre_exposure;
    }
    const float3 color = (sample.rgb / coverage) * inverse_p;
    const float luminance = Luminance(color);
    if (!all(isfinite(color)) || !isfinite(luminance)) {
        InterlockedAdd(s_Histogram[METER_REJECTED], 1u);
        return;
    }
    InterlockedAdd(s_Histogram[METER_FINITE], 1u);
    InterlockedAdd(s_Histogram[METER_WEIGHTED], 1u);
    if (IsDarkMeterSample(luminance, pass.min_log_luminance)) {
        InterlockedAdd(s_Histogram[METER_DARK], 1u);
        if (luminance == 0.0) {
            InterlockedAdd(s_Histogram[METER_BLACK], 1u);
        } else if (luminance > 0.0) {
            InterlockedAdd(s_Histogram[METER_BELOW_WINDOW], 1u);
        }
        // Apply influence to classified dark samples, never to ordinary mass
        // interpolated into bin zero from above the window boundary.
        const uint dark_weight = MeterContribution(weight, true, pass.black_influence);
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

// Full unsigned product using four 16-bit partial products. Each intermediate
// fits uint32, including the carry terms; no optional Int64ShaderOps is needed.
static uint2 MultiplyWide(uint left, uint right)
{
    const uint low_product = (left & 0xffffu) * (right & 0xffffu);
    const uint cross = (left >> 16u) * (right & 0xffffu) + (low_product >> 16u);
    const uint middle = (left & 0xffffu) * (right >> 16u) + (cross & 0xffffu);
    return uint2((middle << 16u) | (low_product & 0xffffu),
        (left >> 16u) * (right >> 16u) + (cross >> 16u) + (middle >> 16u));
}

static float WideToFloat(uint2 words)
{
    return float(words.y) * 4294967296.0 + float(words.x);
}

// Exact integer part of float32-percentile * uint mass, plus a rational
// remainder. Neither large float CDF subtraction nor float64 is required.
struct PercentileBoundary {
    uint whole;
    uint2 remainder; // low, high
    uint shift;
};

static PercentileBoundary MakeBoundary(float percentile, uint mass)
{
    const uint bits = asuint(percentile);
    const uint exponent = (bits >> 23u) & 255u;
    const uint mantissa = (bits & 0x7fffffu) | (exponent != 0u ? 0x800000u : 0u);
    // Valid percentiles [0,1] give shifts in [23,149].
    const uint shift = exponent != 0u ? 150u - exponent : 149u;
    const uint2 product = MultiplyWide(mantissa, mass);
    PercentileBoundary result;
    result.shift = shift;
    result.whole = 0u;
    result.remainder = product;
    if (shift < 32u) {
        result.whole = (product.x >> shift) | (product.y << (32u - shift));
        result.remainder = uint2(product.x & ((1u << shift) - 1u), 0u);
    } else if (shift == 32u) {
        result.whole = product.y;
        result.remainder = uint2(product.x, 0u);
    } else if (shift < 64u) {
        result.whole = product.y >> (shift - 32u);
        result.remainder.y &= (1u << (shift - 32u)) - 1u;
    }
    return result;
}

static float BoundaryFraction(PercentileBoundary boundary)
{
    // Tiny intervals wholly within the first mass unit are handled exactly by
    // the containing-bin branch. This underflow cannot affect a boundary split.
    return boundary.shift > 120u ? 0.0
        : WideToFloat(boundary.remainder) * exp2(-float(boundary.shift));
}

static float BoundaryTail(PercentileBoundary boundary)
{
    if (boundary.shift >= 64u) {
        return 1.0 - BoundaryFraction(boundary);
    }
    uint2 unit = uint2(0u, 0u);
    if (boundary.shift < 32u) {
        unit.x = 1u << boundary.shift;
    } else {
        unit.y = 1u << (boundary.shift - 32u);
    }
    const uint2 remaining = uint2(unit.x - boundary.remainder.x,
        unit.y - boundary.remainder.y - (unit.x < boundary.remainder.x ? 1u : 0u));
    return WideToFloat(remaining) * exp2(-float(boundary.shift));
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
        const uint integer_start = max(cursor, low.whole + (any(low.remainder != 0u) ? 1u : 0u));
        const uint integer_end = min(end, high.whole);
        float overlap = float(integer_end > integer_start ? integer_end - integer_start : 0u);
        if (any(low.remainder != 0u) && cursor <= low.whole && low.whole < end) {
            overlap += BoundaryTail(low);
        }
        if (any(high.remainder != 0u) && cursor <= high.whole && high.whole < end) {
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

static bool GenerationGreater(uint2 left, uint2 right)
{
    return left.y > right.y || (left.y == right.y && left.x > right.x);
}

static ExposureStateData LoadPrevious(uint slot, ExposureTargetData targets)
{
    ExposureStateData state = (ExposureStateData)0;
    const float initial = exp2(targets.initial_log_gain);
    state.displayed_scale = initial;
    state.target_scale = initial;
    state.latent_scale = initial;
    state.latent_target_scale = initial;
    state.fp16_candidate_pre_exposure = 1.0;
    state.fallback_reason = 1u;
    if (slot == K_INVALID_BINDLESS_INDEX) return state;
    ByteAddressBuffer source = ResourceDescriptorHeap[slot];
    const float4 gains = asfloat(source.Load4(0u));
    state.displayed_scale = gains.x;
    state.target_scale = gains.y;
    state.latent_scale = gains.z;
    state.latent_target_scale = gains.w;
    state.raw_metered_luminance = asfloat(source.Load(16u));
    state.raw_metered_ev = asfloat(source.Load(20u));
    state.flags = source.Load(24u);
    state.fallback_reason = source.Load(28u);
    state.settings_revision = source.Load2(32u);
    state.requested_generation = source.Load2(40u);
    state.applied_generation = source.Load2(48u);
    state.frame_sequence = source.Load2(56u);
    state.fp16_candidate_pre_exposure = asfloat(source.Load(64u));
    state.fp16_eligible_streak = source.Load(68u);
    state.product_layout_revision = source.Load2(72u);
    return state;
}

static void StoreState(RWByteAddressBuffer destination, ExposureStateData state)
{
    destination.Store4(0u, asuint(float4(state.displayed_scale, state.target_scale,
        state.latent_scale, state.latent_target_scale)));
    destination.Store2(16u, asuint(float2(state.raw_metered_luminance, state.raw_metered_ev)));
    destination.Store2(24u, uint2(state.flags, state.fallback_reason));
    destination.Store2(32u, state.settings_revision);
    destination.Store2(40u, state.requested_generation);
    destination.Store2(48u, state.applied_generation);
    destination.Store2(56u, state.frame_sequence);
    destination.Store2(64u, uint2(asuint(state.fp16_candidate_pre_exposure), state.fp16_eligible_streak));
    destination.Store2(72u, state.product_layout_revision);
}

static void StoreSolved(RWByteAddressBuffer destination, ExposureStateData state, AutoExposureAverageConstants pass)
{
    StoreState(destination,state);
    if (pass.status_uav == K_INVALID_BINDLESS_INDEX) return;
    RWByteAddressBuffer status = ResourceDescriptorHeap[pass.status_uav];
    status.Store4(0u,uint4(pass.view_lifetime,pass.frame_sequence));
    status.Store4(16u,uint4(pass.settings_revision,state.requested_generation));
    status.Store4(32u,uint4(state.applied_generation,state.product_layout_revision));
    const uint flags = ((state.flags & EXPOSURE_HISTORY_VALID) != 0u ? 1u : 0u)
        | ((state.flags & EXPOSURE_REQUEST_REJECTED) != 0u ? 8u : 0u);
    status.Store4(48u,uint4(flags,0u,0u,state.fp16_eligible_streak));
    status.Store4(64u,uint4(pass.frame_sequence,(state.flags & EXPOSURE_REJECTION_MASK)>>EXPOSURE_REJECTION_SHIFT,0u));
}

[numthreads(1, 1, 1)]
void VortexExposureAverageCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) return;
    StructuredBuffer<AutoExposureAverageConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AutoExposureAverageConstants pass = constants[0];
    if (pass.exposure_buffer_index == K_INVALID_BINDLESS_INDEX || pass.targets_srv == K_INVALID_BINDLESS_INDEX) return;
    RWByteAddressBuffer output = ResourceDescriptorHeap[pass.exposure_buffer_index];
    StructuredBuffer<ExposureTargetData> target_buffer = ResourceDescriptorHeap[pass.targets_srv];
    const ExposureTargetData targets = target_buffer[0];
    ExposureStateData previous = LoadPrevious(pass.previous_state_srv, targets);
    const bool source_loss = (pass.control_flags & 64u) != 0u
        && pass.borrowed_state_srv != K_INVALID_BINDLESS_INDEX;
    if (source_loss) {
        const ExposureStateData source = LoadPrevious(pass.borrowed_state_srv, targets);
        previous.displayed_scale = source.displayed_scale;
        previous.target_scale = source.target_scale;
        previous.latent_scale = source.latent_scale;
        previous.latent_target_scale = source.latent_target_scale;
        previous.raw_metered_luminance = previous.raw_metered_ev = 0.0;
        previous.flags = (previous.flags & ~(EXPOSURE_LUMINANCE_VALID | EXPOSURE_METER_EV_VALID
            | EXPOSURE_HAS_METER_HISTORY | EXPOSURE_SYNTHETIC_DARK | EXPOSURE_ZERO_TARGET))
            | EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED;
    }
    ExposureStateData next = previous;
    next.settings_revision = pass.settings_revision;
    next.frame_sequence = pass.frame_sequence;
    next.flags &= ~(EXPOSURE_LUMINANCE_VALID | EXPOSURE_METER_EV_VALID | EXPOSURE_ZERO_TARGET | EXPOSURE_MODE_MASK);
    next.flags |= pass.exposure_mode << EXPOSURE_MODE_SHIFT;
    const uint captured_rejection = (pass.control_flags >> 2u) & 15u;
    if (captured_rejection != 0u && pass.transition_policy != 0u
        && GenerationGreater(pass.requested_generation, previous.applied_generation)
        && !GenerationGreater(previous.requested_generation, pass.requested_generation)) {
        next.requested_generation = pass.requested_generation;
        next.flags = (next.flags & ~EXPOSURE_REJECTION_MASK) | EXPOSURE_REQUEST_REJECTED
            | (captured_rejection << EXPOSURE_REJECTION_SHIFT);
    }
    if ((pass.control_flags & 2u) != 0u) {
        // Source-defined initialization is a read-only fallback, not a source
        // update or acknowledgement of its pending transition.
        const bool automatic = pass.exposure_mode == 2u;
        const bool seeded = automatic && pass.transition_policy == 3u
            && (pass.control_flags & 1u) == 0u && captured_rejection == 0u;
        const float latent = automatic
            ? exp2(seeded ? pass.seed_log_gain : targets.initial_log_gain)
            : pass.fixed_scale;
        const bool zero = automatic && (targets.flags & 2u) != 0u;
        next.displayed_scale = next.target_scale = zero ? 0.0 : latent;
        next.latent_scale = next.latent_target_scale = latent;
        next.flags |= EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED
            | (zero ? EXPOSURE_ZERO_TARGET : 0u);
        next.fallback_reason = 3u;
        StoreSolved(output, next, pass);
        return;
    }
    const bool borrowing = pass.borrowed_state_srv != K_INVALID_BINDLESS_INDEX && !source_loss;
    const bool new_identity = GenerationGreater(pass.requested_generation, previous.requested_generation);
    const bool already_rejected = all(pass.requested_generation == previous.requested_generation)
        && (previous.flags & EXPOSURE_REQUEST_REJECTED) != 0u;
    bool new_request = pass.transition_policy != 0u
        && captured_rejection == 0u
        && GenerationGreater(pass.requested_generation, previous.applied_generation)
        && !GenerationGreater(previous.requested_generation, pass.requested_generation) && !already_rejected;
    if (new_identity && captured_rejection == 0u) next.flags &= ~(EXPOSURE_REQUEST_REJECTED | EXPOSURE_REJECTION_MASK);
    if (pass.transition_policy != 0u && !GenerationGreater(previous.requested_generation, pass.requested_generation)) {
        next.requested_generation = pass.requested_generation;
    }
    if (new_request && (borrowing || (pass.exposure_mode != 2u && pass.transition_policy != 1u)
        || (pass.transition_policy == 3u && (pass.control_flags & 1u) != 0u))) {
        const uint reason = borrowing ? 3u : pass.exposure_mode != 2u ? 1u : 2u;
        next.flags = (next.flags & ~EXPOSURE_REJECTION_MASK) | EXPOSURE_REQUEST_REJECTED
            | (reason << EXPOSURE_REJECTION_SHIFT);
        new_request = false;
    }
    if (borrowing) {
        const ExposureStateData source = LoadPrevious(pass.borrowed_state_srv, targets);
        next.displayed_scale = source.displayed_scale;
        next.target_scale = source.target_scale;
        next.latent_scale = source.latent_scale;
        next.latent_target_scale = source.latent_target_scale;
        // Local image metering and dormant independent meter history cannot be
        // inferred from a borrowed gain.
        next.raw_metered_luminance = next.raw_metered_ev = 0.0;
        next.flags = (next.flags & ~(EXPOSURE_HAS_METER_HISTORY | EXPOSURE_SYNTHETIC_DARK))
            | EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED | EXPOSURE_BORROWED
            | (source.displayed_scale == 0.0 ? EXPOSURE_ZERO_TARGET : 0u);
        next.fallback_reason = source.fallback_reason;
        StoreSolved(output, next, pass);
        return;
    }
    next.flags &= ~EXPOSURE_BORROWED;
    if (pass.exposure_mode != 2u) {
        // Manual, physical camera and disabled all update the same GPU history.
        next.displayed_scale = pass.fixed_scale;
        next.target_scale = pass.fixed_scale;
        next.latent_scale = pass.fixed_scale;
        next.latent_target_scale = pass.fixed_scale;
        next.flags |= EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED;
        next.fallback_reason = 0u;
        if (new_request) next.applied_generation = pass.requested_generation;
        StoreSolved(output, next, pass);
        return;
    }
    if (targets.key_count == 0u || targets.key_count > 68u) return;
    const bool previous_valid = (previous.flags & EXPOSURE_HISTORY_VALID) != 0u
        && isfinite(previous.latent_scale) && previous.latent_scale > 0.0;
    const float previous_gain = previous_valid ? previous.latent_scale : exp2(targets.initial_log_gain);
    const float previous_log = log2(previous_gain);
    bool dark = false;
    bool valid_meter = false;
    float log_luminance = 0.0;
    if (pass.histogram_buffer_index != K_INVALID_BINDLESS_INDEX) {
        RWByteAddressBuffer histogram = ResourceDescriptorHeap[pass.histogram_buffer_index];
        const uint weighted = histogram.Load(METER_WEIGHTED * 4u);
        dark = weighted > 0u && weighted == histogram.Load(METER_DARK * 4u);
        valid_meter = dark || MeterHistogram(histogram, pass, log_luminance);
        if (dark) log_luminance = pass.min_log_luminance;
    }
    const bool locked = (targets.flags & 1u) != 0u;
    const bool zero = (targets.flags & 2u) != 0u;
    const bool restoring = !zero && (previous.flags & EXPOSURE_ZERO_TARGET) != 0u;
    const bool has_meter_history = (previous.flags & EXPOSURE_HAS_METER_HISTORY) != 0u;
    const float ev = dark ? pass.min_ev : log_luminance - log2(K_MIDDLE_GREY);
    float target_log = previous_log;
    if (locked) target_log = targets.keys[0].y;
    else if (valid_meter) target_log = dark ? targets.dark_log_gain : ResolveLogTarget(targets, ev);
    else if (restoring) target_log = has_meter_history
        ? ((previous.flags & EXPOSURE_SYNTHETIC_DARK) != 0u ? targets.dark_log_gain : ResolveLogTarget(targets, previous.raw_metered_ev))
        : targets.initial_log_gain;
    const bool seed = new_request && pass.transition_policy == 3u;
    const bool remeter = new_request && pass.transition_policy == 2u;
    const bool preserve = new_request && pass.transition_policy == 1u;
    const bool preserve_loss = source_loss && previous_valid && !seed && !remeter && !locked;
    const bool mode_entry = previous_valid && ((previous.flags & EXPOSURE_MODE_MASK) >> EXPOSURE_MODE_SHIFT) != 2u;
    const bool solve = valid_meter || locked || restoring;
    float gain = previous_gain;
    if (seed) gain = exp2(pass.seed_log_gain);
    else if (preserve_loss) gain = previous_gain;
    else if (locked || restoring || (remeter && valid_meter)) gain = exp2(target_log);
    else if (!previous_valid || (previous.flags & EXPOSURE_INITIALIZED) == 0u) {
        if (valid_meter) gain = exp2(target_log);
    } else if (solve && !preserve && !mode_entry && !remeter) {
        const float speed = target_log < previous_log ? pass.log2_speed_up : pass.log2_speed_down;
        if (speed != -256.0 && pass.log2_delta_time != -256.0 && target_log != previous_log) {
            gain = exp2(AdaptLogGain(previous_log, target_log, speed, pass.log2_delta_time, pass.log2_transition_distance));
        }
    }
    const float target = solve ? exp2(target_log) : seed ? gain : previous.latent_target_scale;
    next.displayed_scale = zero ? 0.0 : preserve_loss ? previous.displayed_scale : gain;
    next.target_scale = zero ? 0.0 : target;
    next.latent_scale = gain;
    next.latent_target_scale = target;
    if (valid_meter) {
        next.raw_metered_luminance = exp2(log_luminance);
        next.raw_metered_ev = ev;
        next.flags = (next.flags & ~EXPOSURE_SYNTHETIC_DARK) | EXPOSURE_HAS_METER_HISTORY
            | EXPOSURE_METER_EV_VALID | (dark ? EXPOSURE_SYNTHETIC_DARK : EXPOSURE_LUMINANCE_VALID);
    }
    if (valid_meter || locked || seed) next.flags |= EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED;
    if (restoring && !valid_meter && !has_meter_history && !seed && !locked) next.flags &= ~EXPOSURE_INITIALIZED;
    if (zero) next.flags |= EXPOSURE_ZERO_TARGET;
    if (preserve_loss) next.flags |= EXPOSURE_BORROWED;
    next.fallback_reason = preserve_loss ? 4u : valid_meter || locked || seed ? 0u : 2u;
    if (new_request && (seed || preserve || (remeter && (valid_meter || locked)))) {
        next.applied_generation = pass.requested_generation;
    }
    StoreSolved(output, next, pass);
}

// Pre-scene constants: three uint4 blocks, mirrored by ResolveFrame.
struct ExposureFrameConstants {
    uint output_uav;
    uint current_state_uav;
    uint history_srv;
    uint candidate_srv;
    float fixed_scale;
    float initial_log_gain;
    float seed_log_gain;
    uint mode;
    uint flags;
    uint controls;
    uint current_state_srv;
    uint reserved;
};

[numthreads(1, 1, 1)]
void VortexExposureFrameCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    StructuredBuffer<ExposureFrameConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const ExposureFrameConstants pass = constants[0];
    ExposureTargetData initial = (ExposureTargetData)0;
    initial.initial_log_gain = pass.initial_log_gain;
    ExposureStateData state = LoadPrevious(pass.history_srv, initial);
    const bool history_valid = pass.history_srv != K_INVALID_BINDLESS_INDEX
        && (state.flags & EXPOSURE_HISTORY_VALID) != 0u;
    const bool borrowed = (pass.flags & 2u) != 0u;
    const bool diagnostic = (pass.flags & 4u) != 0u;
    uint flags = pass.flags;
    if (borrowed) {
        // Borrow only numerical gain; no producer request or meter identity.
        state = (ExposureStateData)0;
        ExposureStateData source = LoadPrevious(pass.history_srv, initial);
        state.displayed_scale = source.displayed_scale;
        state.target_scale = source.target_scale;
        state.latent_scale = source.latent_scale;
        state.latent_target_scale = source.latent_target_scale;
        state.flags = EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED | EXPOSURE_BORROWED;
    } else if (diagnostic || (!history_valid && pass.mode != 2u)) {
        const float fixed_gain = diagnostic || pass.mode == 3u ? 1.0 : pass.fixed_scale;
        state.displayed_scale = fixed_gain;
        state.target_scale = fixed_gain;
        state.latent_scale = fixed_gain;
        state.latent_target_scale = fixed_gain;
        state.flags = EXPOSURE_HISTORY_VALID | EXPOSURE_INITIALIZED | (pass.mode << EXPOSURE_MODE_SHIFT);
    } else if (!history_valid && pass.mode == 2u && (pass.controls & 1u) == 0u) {
        flags |= 1u;
    }
    if (!borrowed && !diagnostic && (pass.controls & 2u) != 0u) {
        state.displayed_scale = 0.0;
        state.target_scale = 0.0;
        state.flags |= EXPOSURE_ZERO_TARGET;
    }
    float p = state.displayed_scale > 0.0 ? state.displayed_scale : state.latent_scale;
    if (!borrowed && !diagnostic) {
        if (pass.mode != 2u) p = pass.mode == 3u ? 1.0 : pass.fixed_scale;
        else if ((pass.controls & 1u) != 0u) p = exp2(pass.seed_log_gain);
    }
    if (pass.candidate_srv != K_INVALID_BINDLESS_INDEX) {
        const ExposureStateData candidate = LoadPrevious(pass.candidate_srv, initial);
        if ((candidate.flags & 256u) != 0u && candidate.fp16_eligible_streak >= 2u
            && isfinite(candidate.fp16_candidate_pre_exposure)
            && candidate.fp16_candidate_pre_exposure >= exp2(-32.0)
            && candidate.fp16_candidate_pre_exposure <= exp2(32.0))
            p = candidate.fp16_candidate_pre_exposure;
        else
            flags |= 1u;
    }
    if (!isfinite(p) || p <= 0.0) {
        flags |= 1u;
        p = 1.0;
    }
    p = clamp(p, exp2(-32.0), exp2(32.0));
    if ((flags & (1u | 4u | 8u)) != 0u) p = 1.0;
    if ((flags & 8u) != 0u) flags |= 1u;
    RWByteAddressBuffer current = ResourceDescriptorHeap[pass.current_state_uav];
    StoreState(current, state);
    FrameExposureData frame;
    frame.pre_exposure = p;
    frame.one_over_pre_exposure = rcp(p);
    frame.global_exposure_state_slot = borrowed ? pass.history_srv : pass.current_state_srv;
    frame.flags = flags;
    RWStructuredBuffer<FrameExposureData> output = ResourceDescriptorHeap[pass.output_uav];
    output[0] = frame;
}

// Preserve the frame's state address when the final solve could not submit.
// Only gain and the zero-target tag change; request/meter identity is never
// imported from a removed source or acknowledged by this fallback copy.
[numthreads(1, 1, 1)]
void VortexExposureFallbackCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    StructuredBuffer<ExposureFrameConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const ExposureFrameConstants pass = constants[0];
    ByteAddressBuffer source = ResourceDescriptorHeap[pass.history_srv];
    RWByteAddressBuffer destination = ResourceDescriptorHeap[pass.current_state_uav];
    uint4 gains = source.Load4(0u);
    uint flags = destination.Load(EXPOSURE_FLAGS_OFFSET) & ~EXPOSURE_ZERO_TARGET;
    if ((pass.controls & 2u) != 0u) {
        gains.xy = 0u.xx;
        flags |= EXPOSURE_ZERO_TARGET;
    }
    destination.Store4(0u, gains);
    destination.Store(EXPOSURE_FLAGS_OFFSET, flags);
}

struct SuitabilityConstants {
    uint report_uav; uint source_srv; uint frame_srv; uint state_srv;
    uint width; uint height; uint depth; uint flags;
    uint product; uint expected_mask; uint mask_srv; uint meter_mode;
    uint left; uint top; uint meter_width; uint meter_height;
    float radius; float budget_share; float min_log_luminance; float black_influence;
};

static void SuitabilityFailure(RWByteAddressBuffer report, uint product, uint flags, uint counter)
{
    uint unused;
    report.InterlockedOr(12u, flags, unused);
    report.InterlockedCompareExchange(16u, 0u, product, unused);
    report.InterlockedAdd(counter, 1u, unused);
}

static float4 SuitabilitySample(SuitabilityConstants pass, uint3 pixel)
{
    if ((pass.flags & 8u) != 0u) {
        Texture3D<float4> source = ResourceDescriptorHeap[pass.source_srv];
        return source.Load(int4(pixel, 0));
    }
    Texture2D<float4> source = ResourceDescriptorHeap[pass.source_srv];
    return source.Load(int3(pixel.xy, 0));
}

[numthreads(1, 1, 1)]
void ClearSuitability(uint3 pixel : SV_DispatchThreadID)
{
    StructuredBuffer<SuitabilityConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SuitabilityConstants pass = constants[0];
    RWByteAddressBuffer report = ResourceDescriptorHeap[pass.report_uav];
    report.Store4(0u, uint4(asuint(1.0), 0u, 0u, 0u));
    report.Store4(16u, 0u.xxxx);
    report.Store4(32u, uint4(0u, 0u, pass.expected_mask, 0u));
}

[numthreads(8, 8, 1)]
void GatherSuitabilityMaximum(uint3 pixel : SV_DispatchThreadID)
{
    StructuredBuffer<SuitabilityConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SuitabilityConstants pass = constants[0];
    if (pixel.x >= pass.width || pixel.y >= pass.height || pixel.z >= pass.depth) return;
    RWByteAddressBuffer report = ResourceDescriptorHeap[pass.report_uav];
    uint unused;
    if (all(pixel == 0u.xxx)) report.InterlockedOr(8u, 1u << (pass.product - 1u), unused);
    const float4 sample = SuitabilitySample(pass, pixel);
    if (!all(isfinite(sample))) {
        SuitabilityFailure(report, pass.product, 1u, 20u);
        return;
    }
    StructuredBuffer<FrameExposureData> frame = ResourceDescriptorHeap[pass.frame_srv];
    const float3 scene = abs(sample.rgb) * frame[0].one_over_pre_exposure;
    const float maximum = max(scene.x, max(scene.y, scene.z));
    report.InterlockedMax(4u, asuint(maximum), unused);
}

[numthreads(1, 1, 1)]
void SelectSuitabilityCandidate(uint3 pixel : SV_DispatchThreadID)
{
    StructuredBuffer<SuitabilityConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SuitabilityConstants pass = constants[0];
    RWByteAddressBuffer report = ResourceDescriptorHeap[pass.report_uav];
    const float maximum = asfloat(report.Load(4u));
    float p = maximum > 0.0 ? exp2(clamp(floor(log2(16376.0) - log2(maximum)), -32.0, 32.0)) : 1.0;
    if (maximum * p > 16376.0 && p > exp2(-32.0)) p *= 0.5;
    report.Store(0u, asuint(p));
    const uint missing = pass.expected_mask & ~report.Load(8u);
    if (pass.expected_mask == 0u || missing != 0u) {
        uint unused;
        report.InterlockedOr(12u, 16u, unused);
        if (missing != 0u) report.InterlockedCompareExchange(16u, 0u, uint(firstbitlow(missing)) + 1u, unused);
    }
}

static bool SuitabilityMeterCell(SuitabilityConstants pass, uint2 pixel, out uint2 cell, out float2 uv)
{
    cell = 0u.xx; uv = 0.0.xx;
    if ((pass.flags & 1u) == 0u || pass.meter_width == 0u || pass.meter_height == 0u) return false;
    if (pixel.x < pass.left || pixel.y < pass.top || pixel.x >= pass.left + pass.meter_width || pixel.y >= pass.top + pass.meter_height) return false;
    const uint2 extent = uint2(pass.meter_width, pass.meter_height);
    const uint2 grid = min(extent, METER_GRID_LIMIT);
    const uint2 relative = pixel - uint2(pass.left, pass.top);
    cell = min(uint2((float2(relative) + 0.5) * float2(grid) / float2(extent)), grid - 1u);
    uv = (float2(cell) + 0.5) / float2(grid);
    return all(relative == min(uint2(uv * float2(extent)), extent - 1u));
}

[numthreads(8, 8, 1)]
void CheckSuitabilityProduct(uint3 pixel : SV_DispatchThreadID)
{
    StructuredBuffer<SuitabilityConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SuitabilityConstants pass = constants[0];
    if (pixel.x >= pass.width || pixel.y >= pass.height || pixel.z >= pass.depth) return;
    RWByteAddressBuffer report = ResourceDescriptorHeap[pass.report_uav];
    const float4 sample = SuitabilitySample(pass, pixel);
    if (!all(isfinite(sample))) return;
    uint unused;
    report.InterlockedAdd(36u, 1u, unused);
    StructuredBuffer<FrameExposureData> frame = ResourceDescriptorHeap[pass.frame_srv];
    ByteAddressBuffer state = ResourceDescriptorHeap[pass.state_srv];
    const float s = asfloat(state.Load(0u));
    const float inverse_p = frame[0].one_over_pre_exposure;
    const float candidate_p = asfloat(report.Load(0u));
    const float3 scene = sample.rgb * inverse_p;
    const float3 proposed = scene * candidate_p;
    if (any(abs(proposed) > 16376.0)) {
        SuitabilityFailure(report, pass.product, 2u, 32u);
        return;
    }
    const float3 restored = f16tof32(f32tof16(proposed)) / candidate_p;
    const float alpha = (pass.flags & 2u) != 0u ? saturate(sample.a) : 1.0;
    const float narrowed_alpha = (pass.flags & 2u) != 0u ? f16tof32(f32tof16(alpha)) : 1.0;
    const float3 reference = scene / max(alpha, 1e-6);
    const float3 narrowed = restored / max(narrowed_alpha, 1e-6);
    const float3 error = abs(reference - narrowed);
    const float relative_budget = .0025 * pass.budget_share;
    const float absolute_budget = 1e-5 * pass.budget_share;
    bool image_failure = any(error > abs(reference) * relative_budget + absolute_budget)
        || any(error * s > abs(reference * s) * relative_budget + absolute_budget);
    if ((pass.flags & 4u) != 0u) {
        const float t = saturate(sample.a);
        const float dt = abs(f16tof32(f32tof16(t)) - t);
        const float maximum = asfloat(report.Load(4u));
        image_failure = image_failure || dt > t * relative_budget + absolute_budget / max(maximum * max(s, 1.0), 1e-30);
    }
    if (image_failure) SuitabilityFailure(report, pass.product, 4u, 28u);
    uint2 cell; float2 uv;
    if (!SuitabilityMeterCell(pass, pixel.xy, cell, uv)) return;
    float mask = 1.0;
    if (pass.mask_srv != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float4> mask_texture = ResourceDescriptorHeap[pass.mask_srv];
        SamplerState mask_sampler = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
        mask = mask_texture.SampleLevel(mask_sampler, uv, 0).r;
    }
    if (!isfinite(mask)) {
        SuitabilityFailure(report, pass.product, 1u, 20u);
        return;
    }
    const uint2 grid = min(uint2(pass.meter_width, pass.meter_height), METER_GRID_LIMIT);
    const float distance = length((float2(cell * 2u + 1u) - float2(grid)) / float2(grid));
    float profile = 1.0;
    if (pass.meter_mode == 1u) profile = saturate(1.0 - distance);
    else if (pass.meter_mode == 2u) {
        profile = pass.radius > 0.0 ? saturate(1.0 - distance / pass.radius) : (all(cell * 2u + 1u == grid) ? 1.0 : 0.0);
        profile *= profile;
    }
    const uint weight = QuantizeWeight(profile * saturate(mask) * alpha);
    const uint narrowed_weight = QuantizeWeight(profile * saturate(mask) * narrowed_alpha);
    if (weight == 0u && narrowed_weight == 0u) return;
    const float luminance = Luminance(reference);
    const float narrowed_luminance = Luminance(narrowed);
    const bool dark = IsDarkMeterSample(luminance, pass.min_log_luminance);
    const bool narrowed_dark = IsDarkMeterSample(narrowed_luminance, pass.min_log_luminance);
    const uint mass = MeterContribution(weight, dark, pass.black_influence);
    const uint narrowed_mass = MeterContribution(narrowed_weight, narrowed_dark, pass.black_influence);
    // Weighted/dark classification controls the aggregate synthetic-dark
    // fallback even when black influence removes all histogram mass.
    bool meter_failure = ((weight == 0u) != (narrowed_weight == 0u))
        || dark != narrowed_dark || mass != narrowed_mass;
    if (mass != 0u || narrowed_mass != 0u) {
        meter_failure = meter_failure || ((luminance == 0.0) != (narrowed_luminance == 0.0))
            || (luminance > 0.0 && (narrowed_luminance <= 0.0
                || abs(log2(narrowed_luminance / luminance)) > 1.0 / 1024.0));
    }
    if (meter_failure) SuitabilityFailure(report, pass.product, 8u, 24u);
}
