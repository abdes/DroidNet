//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI
#define OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"

// Loads a frame exposure publication; neutral exposure when the slot is not
// published. Passes without a camera view use this with their own slot.
static FrameExposureData LoadFrameExposure(uint frame_exposure_slot)
{
    if (frame_exposure_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(frame_exposure_slot)) {
        FrameExposureData neutral;
        neutral.pre_exposure = 1.0f;
        neutral.one_over_pre_exposure = 1.0f;
        neutral.global_exposure_state_slot = K_INVALID_BINDLESS_INDEX;
        neutral.flags = 0u;
        return neutral;
    }
    StructuredBuffer<FrameExposureData> buffer
        = ResourceDescriptorHeap[frame_exposure_slot];
    return buffer[0];
}

#endif // OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI
