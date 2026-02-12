//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Common/Math.hlsli"

#define HISTOGRAM_BINS 256

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AutoExposureAverageConstants {
    uint histogram_buffer_index;
    uint exposure_buffer_index;
    float min_log_luminance;
    float log_luminance_range;
    float low_percentile;
    float high_percentile;
    float adaptation_speed_up;
    float adaptation_speed_down;
    float delta_time;
    float target_luminance;
};

// exposure_buffer structure:
// [0] = average luminance (smoothed)
// [4] = exposure multiplier
// [8] = resolved EV (EV100, smoothed, derived from avg luminance)

[numthreads(1, 1, 1)]
void CS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<AutoExposureAverageConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    RWByteAddressBuffer histogramBuffer = ResourceDescriptorHeap[pass.histogram_buffer_index];
    RWByteAddressBuffer exposureBuffer = ResourceDescriptorHeap[pass.exposure_buffer_index];

    // Previous frame state. Keep this as the fallback when histogram data is
    // missing/invalid so we don't explode exposure (white-out).
    float prevLum = asfloat(exposureBuffer.Load(0));
    if (prevLum <= 0.0) {
        prevLum = max(pass.target_luminance, 0.0001);
    }
    prevLum = max(prevLum, 1e-6);
    float prevLogLum = log2(prevLum);

    uint count = 0;
    for (uint i = 0; i < HISTOGRAM_BINS; ++i) {
        count += histogramBuffer.Load(i * 4);
    }

    // We compute and smooth in log2 luminance space ("stops"). This makes
    // adaptation_speed_up/down behave like EV per second and avoids
    // instability when luminance spans many orders of magnitude.
    float targetLogLum = prevLogLum;
    if (count > 0) {
        float lowP = saturate(pass.low_percentile);
        float highP = saturate(pass.high_percentile);
        highP = max(highP, lowP);

        float lowBound = lowP * float(count);
        float highBound = highP * float(count);

        uint cumulativeCount = 0;
        float validWeight = 0.0;
        float weightedSum = 0.0;

        for (uint j = 0; j < HISTOGRAM_BINS; ++j) {
            uint binValue = histogramBuffer.Load(j * 4);

            // Current bin range in pixel count space
            uint binStart = cumulativeCount;
            uint binEnd = cumulativeCount + binValue;
            cumulativeCount += binValue;

            // Intersect bin range [binStart, binEnd] with filter range [lowBound, highBound]
            float overlapStart = max(float(binStart), lowBound);
            float overlapEnd = min(float(binEnd), highBound);
            float overlapCount = max(0.0, overlapEnd - overlapStart);

            if (overlapCount > 0.0) {
                float logLum = pass.min_log_luminance + (float(j) / float(HISTOGRAM_BINS - 1)) * pass.log_luminance_range;
                weightedSum += logLum * overlapCount;
                validWeight += overlapCount;
            }
        }

        if (validWeight > 0.0) {
            targetLogLum = weightedSum / validWeight;
        }
    }

    // Temporal smoothing
    float diff = targetLogLum - prevLogLum;
    float speedUp = max(pass.adaptation_speed_up, 0.0);
    float speedDown = max(pass.adaptation_speed_down, 0.0);
    float speed = (diff > 0.0) ? speedUp : speedDown;

    float dt = max(pass.delta_time, 0.0);
    float t = 1.0 - exp(-dt * speed);
    t = saturate(t);

    float smoothedLogLum = lerp(prevLogLum, targetLogLum, t);
    float avgLum = exp2(smoothedLogLum);
    avgLum = max(avgLum, 0.0001);

    // Calculate exposure
    // Exposure = target_luminance / average_luminance
    float exposure = pass.target_luminance / avgLum;

    // Safety clamp: this buffer feeds tonemap as a *multiplier*.
    // Keep it within a practical range to avoid pathological blow-outs.
    // Extended range to support high exposure compensation.
    exposure = clamp(exposure, 1.0e-8, 64000.0);

    // Guard against NaNs/Infs turning into nonsense downstream.
    if (exposure != exposure || abs(exposure) > 1.0e6) {
        exposure = 1.0;
    }

    // Derive EV (EV100, ISO 100 reference) from the smoothed average luminance
    // using the ISO 2720
    // reflected-light calibration constant K=12.5:
    //   EV100 = log2( L * 100 / K )
    // This is useful for UI/debugging and to keep auto-exposure semantics
    // aligned with the engine's EV100-based exposure model.
    const float kCalibrationK = 12.5;
    float ev = log2(max(1.0e-4, avgLum * 100.0 / kCalibrationK));

    exposureBuffer.Store(0, asuint(avgLum));
    exposureBuffer.Store(4, asuint(exposure));
    exposureBuffer.Store(8, asuint(ev));
    exposureBuffer.Store(12, count); // Debug: Store histogram sample count
}
