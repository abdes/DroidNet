//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI
#define OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ExposureStateData.hlsli"

// Load a frame exposure publication; false, with a zeroed record, when the
// slot is missing or outside the global SRV domain. There is no neutral
// default: the renderer publishes exposure for every view.
static bool TryLoadFrameExposure(uint frame_exposure_slot, out FrameExposureData exposure)
{
    exposure = (FrameExposureData)0;
    exposure.global_exposure_state_slot = K_INVALID_BINDLESS_INDEX;
    if (frame_exposure_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(frame_exposure_slot)) {
        return false;
    }
    StructuredBuffer<FrameExposureData> buffer
        = ResourceDescriptorHeap[frame_exposure_slot];
    exposure = buffer[0];
    return true;
}

#endif // OXYGEN_VORTEX_FRAME_EXPOSURE_HLSLI
