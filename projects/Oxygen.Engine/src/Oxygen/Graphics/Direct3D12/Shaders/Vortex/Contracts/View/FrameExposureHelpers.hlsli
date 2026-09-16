//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FRAME_EXPOSURE_HELPERS_HLSLI
#define OXYGEN_VORTEX_FRAME_EXPOSURE_HELPERS_HLSLI

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

static FrameExposureData GetFrameExposure()
{
    FrameExposureData fallback;
    fallback.pre_exposure = 1.0f;
    fallback.one_over_pre_exposure = 1.0f;
    fallback.global_exposure_state_slot = K_INVALID_BINDLESS_INDEX;
    fallback.flags = 0u;
    const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const uint slot = bindings.frame_exposure_slot;
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return fallback;
    }
    StructuredBuffer<FrameExposureData> buffer = ResourceDescriptorHeap[slot];
    return buffer[0];
}

static float GetPreExposure()
{
    return GetFrameExposure().pre_exposure;
}

static float GetOneOverPreExposure()
{
    return GetFrameExposure().one_over_pre_exposure;
}

#endif
