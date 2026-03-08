//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI

#include "Renderer/DrawFrameBindings.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

static inline DrawFrameBindings LoadResolvedDrawFrameBindings()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    return LoadDrawFrameBindings(view_bindings.draw_frame_slot);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI
