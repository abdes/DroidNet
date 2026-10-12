//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUG_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUG_HLSLI

// Pure GPU debug helpers: they take the view's GpuDebugBindings, which a pass
// carries in its inputs record. Every helper is a no-op without bindings, so
// producers need no other guard. Camera passes use GpuDebugView.hlsli.

#include "Vortex/Contracts/Diagnostics/GpuDebugBindings.hlsli"

// Mirrors oxygen::vortex::GpuDebugLine.
struct GpuDebugLine
{
    float4 world_pos0;
    float4 world_pos1;
    float4 color0;
    float4 color1;
};

// Appends a line. Lines past the capacity are counted, not stored.
static inline void AddGpuDebugLine(
    GpuDebugBindings debug, GpuDebugLine debug_line)
{
    if (debug.line_buffer_uav_slot == K_INVALID_BINDLESS_INDEX
        || debug.counter_buffer_uav_slot == K_INVALID_BINDLESS_INDEX) {
        return;
    }
    RWStructuredBuffer<uint> counters
        = ResourceDescriptorHeap[debug.counter_buffer_uav_slot];
    uint index;
    InterlockedAdd(counters[GPU_DEBUG_COUNTER_REQUESTED_LINES], 1u, index);
    if (index >= debug.line_capacity) {
        return;
    }
    RWStructuredBuffer<GpuDebugLine> lines
        = ResourceDescriptorHeap[debug.line_buffer_uav_slot];
    lines[index] = debug_line;
    // Every index below the capacity is written, so the drawn count ends at
    // min(requested, capacity).
    InterlockedAdd(counters[GPU_DEBUG_COUNTER_DRAWN_LINES], 1u);
}

static inline void AddGpuDebugLine(
    GpuDebugBindings debug, float3 p0, float3 p1, float3 color)
{
    GpuDebugLine debug_line;
    debug_line.world_pos0 = float4(p0, 1.0f);
    debug_line.world_pos1 = float4(p1, 1.0f);
    debug_line.color0 = float4(color, 1.0f);
    debug_line.color1 = debug_line.color0;
    AddGpuDebugLine(debug, debug_line);
}

static inline void AddGpuDebugCross(
    GpuDebugBindings debug, float3 p, float3 color, float radius)
{
    AddGpuDebugLine(debug, p - float3(radius, 0.0f, 0.0f),
        p + float3(radius, 0.0f, 0.0f), color);
    AddGpuDebugLine(debug, p - float3(0.0f, radius, 0.0f),
        p + float3(0.0f, radius, 0.0f), color);
    AddGpuDebugLine(debug, p - float3(0.0f, 0.0f, radius),
        p + float3(0.0f, 0.0f, radius), color);
}

// The 12 edges of the box `center` +- `axis_x`, `axis_y`, `axis_z`.
static inline void AddGpuDebugBox(GpuDebugBindings debug, float3 center,
    float3 axis_x, float3 axis_y, float3 axis_z, float3 color)
{
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const float3 signs = float3((corner & 1u) != 0u ? 1.0f : -1.0f,
            (corner & 2u) != 0u ? 1.0f : -1.0f,
            (corner & 4u) != 0u ? 1.0f : -1.0f);
        const float3 from
            = center + signs.x * axis_x + signs.y * axis_y + signs.z * axis_z;
        if ((corner & 1u) == 0u) {
            AddGpuDebugLine(debug, from, from + 2.0f * axis_x, color);
        }
        if ((corner & 2u) == 0u) {
            AddGpuDebugLine(debug, from, from + 2.0f * axis_y, color);
        }
        if ((corner & 4u) == 0u) {
            AddGpuDebugLine(debug, from, from + 2.0f * axis_z, color);
        }
    }
}

// Counts an invalid access in `counter` and keeps its slot.
static inline void CountGpuDebugInvalidAccess(
    GpuDebugBindings debug, uint counter, uint slot)
{
    if (debug.counter_buffer_uav_slot == K_INVALID_BINDLESS_INDEX) {
        return;
    }
    RWStructuredBuffer<uint> counters
        = ResourceDescriptorHeap[debug.counter_buffer_uav_slot];
    InterlockedAdd(counters[counter], 1u);
    uint previous;
    InterlockedExchange(
        counters[GPU_DEBUG_COUNTER_LAST_INVALID_SLOT], slot, previous);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_DIAGNOSTICS_GPUDEBUG_HLSLI
