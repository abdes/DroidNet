//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_EXPOSURE_STATE_DATA_HLSLI
#define OXYGEN_VORTEX_EXPOSURE_STATE_DATA_HLSLI

// C++ mirror: Vortex/Types/ExposureStateData.h. Byte-address access offsets
// below are qualified by native capture tests, not inferred from cbuffer packing.
struct FrameExposureData {
    float pre_exposure;
    float one_over_pre_exposure;
    uint global_exposure_state_slot;
    uint flags;
};

struct ExposureStateData {
    float displayed_scale;
    float target_scale;
    float latent_scale;
    float latent_target_scale;
    float raw_metered_luminance;
    float raw_metered_ev;
    uint flags;
    uint fallback_reason;
    uint2 settings_revision;
    uint2 requested_generation;
    uint2 applied_generation;
    uint2 frame_sequence;
    float fp16_candidate_pre_exposure;
    uint fp16_eligible_streak;
    uint2 product_layout_revision;
};

static const uint EXPOSURE_HISTORY_VALID = 1u;
static const uint EXPOSURE_INITIALIZED = 2u;
static const uint EXPOSURE_LUMINANCE_VALID = 4u;
static const uint EXPOSURE_METER_EV_VALID = 8u;
static const uint EXPOSURE_SYNTHETIC_DARK = 16u;
static const uint EXPOSURE_RANGE_FAILURE = 32u;
static const uint EXPOSURE_ZERO_TARGET = 64u;
static const uint EXPOSURE_BORROWED = 128u;
static const uint EXPOSURE_FP16_ELIGIBLE = 256u;
static const uint EXPOSURE_HAS_METER_HISTORY = 512u;
static const uint EXPOSURE_MODE_SHIFT = 10u;
static const uint EXPOSURE_MODE_MASK = 3u << EXPOSURE_MODE_SHIFT;
static const uint EXPOSURE_REQUEST_REJECTED = 1u << 12u;
static const uint EXPOSURE_REJECTION_SHIFT = 16u;
static const uint EXPOSURE_REJECTION_MASK = 15u << EXPOSURE_REJECTION_SHIFT;
static const uint EXPOSURE_DISPLAYED_SCALE_OFFSET = 0u;
static const uint EXPOSURE_LATENT_SCALE_OFFSET = 8u;
static const uint EXPOSURE_METER_EV_OFFSET = 20u;
static const uint EXPOSURE_FLAGS_OFFSET = 24u;

// Product 11 is the resolved SceneColor boundary in the HDR inventory.
static bool IsCheckedSceneColorAccepted(ByteAddressBuffer report)
{
    const uint product_mask = 1u << 10u;
    return report.Load(12u) == 0u && report.Load(8u) == product_mask
        && report.Load(40u) == product_mask;
}

#endif
