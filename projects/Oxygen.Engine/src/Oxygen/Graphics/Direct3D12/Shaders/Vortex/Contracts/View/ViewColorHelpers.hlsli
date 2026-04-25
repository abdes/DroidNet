//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORHELPERS_HLSLI

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewColorData.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

static inline float GetExposure()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.view_color_frame_slot != K_INVALID_BINDLESS_INDEX) {
        const ViewColorData view_color =
            LoadViewColorData(view_bindings.view_color_frame_slot);
        return max(view_color.exposure, 0.0f);
    }

    return 1.0f;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORHELPERS_HLSLI
