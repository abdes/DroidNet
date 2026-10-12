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

// Camera-view exposure: the bound view's frame exposure publication.
// The bound view's exposure. A view without one, which the renderer rejects
// before recording, reads zero pre-exposure and renders black rather than at a
// plausible neutral exposure.
static FrameExposureData GetFrameExposure()
{
    const ViewFrameBindings bindings
        = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    FrameExposureData exposure;
    TryLoadFrameExposureData(bindings.frame_exposure_slot, exposure);
    return exposure;
}

static float GetPreExposure()
{
    return GetFrameExposure().pre_exposure;
}

static float GetOneOverPreExposure()
{
    return GetFrameExposure().one_over_pre_exposure;
}

// The bound view's exposure status UAV; invalid when the view has no exposure
// state to report HDR range failures to.
static uint GetExposureStatusUav()
{
    return LoadViewFrameBindings(bindless_view_frame_bindings_slot).exposure_status_uav;
}

static bool IsFp32OnlyExposure()
{
    return (GetFrameExposure().flags & 16u) != 0u;
}

#endif
