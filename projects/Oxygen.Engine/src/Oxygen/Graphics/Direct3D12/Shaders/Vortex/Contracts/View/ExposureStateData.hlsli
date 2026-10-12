//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_EXPOSURE_STATE_DATA_HLSLI
#define OXYGEN_VORTEX_EXPOSURE_STATE_DATA_HLSLI

#include "Vortex/Contracts/View/Generated.ExposureContracts.hlsli"

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

// Product 11 is the resolved SceneColor boundary in the HDR inventory.
static bool IsCheckedSceneColorAccepted(ByteAddressBuffer report)
{
    const uint product_mask = 1u << 10u;
    return report.Load(HDR_SUITABILITY_FAILURE_FLAGS_OFFSET) == 0u
        && report.Load(HDR_SUITABILITY_CHECKED_PRODUCTS_OFFSET) == product_mask
        && report.Load(HDR_SUITABILITY_EXPECTED_PRODUCTS_OFFSET) == product_mask;
}

#endif
