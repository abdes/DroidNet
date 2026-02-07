//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_GPUDEBUG_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_GPUDEBUG_HLSLI

#include "Renderer/SceneConstants.hlsli"

struct GpuDebugLine
{
    float4 worldPos0;
    float4 worldPos1;
    float4 colorAlpha0;
    float4 colorAlpha1;
};

#define MAX_GPU_DEBUG_LINES (128 * 1024)

void AddGpuDebugLine(GpuDebugLine debugLine)
{
    if (bindless_gpu_debug_line_slot == 0 || bindless_gpu_debug_counter_slot == 0)
    {
        return;
    }

    RWStructuredBuffer<GpuDebugLine> gpuDebugLineBuffer = ResourceDescriptorHeap[bindless_gpu_debug_line_slot];
    RWByteAddressBuffer gpuDebugLineCounterBuffer = ResourceDescriptorHeap[bindless_gpu_debug_counter_slot];

    uint newNodeSlot;
    // The counter for InstanceCount is at byte offset 4 of the indirect args buffer
    gpuDebugLineCounterBuffer.InterlockedAdd(4, 1, newNodeSlot);

    if (newNodeSlot < MAX_GPU_DEBUG_LINES)
    {
        gpuDebugLineBuffer[newNodeSlot] = debugLine;
    }
}

void AddGpuDebugLine(float3 p0, float3 p1, float3 c)
{
    GpuDebugLine gdl;
    gdl.colorAlpha0 = gdl.colorAlpha1 = float4(c, 1.0);
    gdl.worldPos0 = float4(p0, 1.0);
    gdl.worldPos1 = float4(p1, 1.0);
    AddGpuDebugLine(gdl);
}

void AddGpuDebugCross(float3 p, float3 c, float r)
{
    GpuDebugLine gdl;
    gdl.colorAlpha0 = gdl.colorAlpha1 = float4(c, 1.0);
    gdl.worldPos0 = float4(p + float3(-r, 0, 0), 1.0);
    gdl.worldPos1 = float4(p + float3( r, 0, 0), 1.0);
    AddGpuDebugLine(gdl);
    gdl.worldPos0 = float4(p + float3(0, -r, 0), 1.0);
    gdl.worldPos1 = float4(p + float3(0,  r, 0), 1.0);
    AddGpuDebugLine(gdl);
    gdl.worldPos0 = float4(p + float3(0, 0, -r), 1.0);
    gdl.worldPos1 = float4(p + float3(0, 0,  r), 1.0);
    AddGpuDebugLine(gdl);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_GPUDEBUG_HLSLI
