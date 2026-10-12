//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DIAGNOSTICS_GPUDEBUGBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DIAGNOSTICS_GPUDEBUGBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

// Mirrors oxygen::vortex::DebugFrameBindings.
struct GpuDebugBindings
{
    uint line_buffer_srv_slot;
    uint line_buffer_uav_slot;
    uint counter_buffer_uav_slot;
    uint line_capacity;
};

// The counter words, one uint each; mirrors oxygen::vortex::GpuDebugCounters.
// Words 0 to 3 are the D3D12_DRAW_ARGUMENTS of the line draw.
static const uint GPU_DEBUG_COUNTER_VERTEX_COUNT = 0u;
static const uint GPU_DEBUG_COUNTER_DRAWN_LINES = 1u;
static const uint GPU_DEBUG_COUNTER_REQUESTED_LINES = 4u;
static const uint GPU_DEBUG_COUNTER_INVALID_BINDLESS = 5u;
static const uint GPU_DEBUG_COUNTER_INVALID_MATERIAL = 6u;
static const uint GPU_DEBUG_COUNTER_LAST_INVALID_SLOT = 7u;
static const uint GPU_DEBUG_COUNTER_COUNT = 8u;

static GpuDebugBindings LoadGpuDebugBindings(uint slot)
{
    GpuDebugBindings invalid_bindings;
    invalid_bindings.line_buffer_srv_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.line_buffer_uav_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.counter_buffer_uav_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.line_capacity = 0u;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<GpuDebugBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DIAGNOSTICS_GPUDEBUGBINDINGS_HLSLI
