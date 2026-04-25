//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI

#include "Vortex/Contracts/Draw/DrawFrameBindings.hlsli"
#include "Vortex/Contracts/Draw/VelocityPublications.hlsli"
#include "Vortex/Contracts/View/ViewHistoryFrameBindings.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

static inline ViewFrameBindings LoadResolvedViewFrameBindings()
{
    return LoadViewFrameBindings(bindless_view_frame_bindings_slot);
}

static inline DrawFrameBindings LoadResolvedDrawFrameBindings()
{
    const ViewFrameBindings view_bindings = LoadResolvedViewFrameBindings();
    return LoadDrawFrameBindings(view_bindings.draw_frame_slot);
}

static inline ViewHistoryFrameBindings LoadResolvedViewHistoryFrameBindings()
{
    const ViewFrameBindings view_bindings = LoadResolvedViewFrameBindings();
    return LoadViewHistoryFrameBindings(view_bindings.history_frame_slot);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWHELPERS_HLSLI
