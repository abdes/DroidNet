//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUGVIEW_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUGVIEW_HLSLI

// Camera-only GPU debug helpers: they reach the bound view's GpuDebugBindings
// through its view frame bindings.

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Diagnostics/GpuDebug.hlsli"

// The bound view's GPU debug bindings.
static inline GpuDebugBindings LoadViewGpuDebugBindings()
{
    return LoadGpuDebugBindings(
        LoadViewFrameBindings(bindless_view_frame_bindings_slot)
            .debug_frame_slot);
}

// Reports a bindless load of the bound view that failed its checks.
static inline void ReportGpuDebugInvalidBindless(uint slot)
{
    CountGpuDebugInvalidAccess(
        LoadViewGpuDebugBindings(), GPU_DEBUG_COUNTER_INVALID_BINDLESS, slot);
}

// Reports a draw of the bound view whose material could not be loaded from
// the materials slot `slot`.
static inline void ReportGpuDebugInvalidMaterial(uint slot)
{
    CountGpuDebugInvalidAccess(
        LoadViewGpuDebugBindings(), GPU_DEBUG_COUNTER_INVALID_MATERIAL, slot);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUGVIEW_HLSLI
