//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Lighting/LightGridData.hlsli"
#include "Vortex/Contracts/Shadows/ShadowRecords.hlsli"

cbuffer ProbeRoot : register(b2, space0) {
    uint g_RecordKind;
    uint g_ProbeArguments;
};

// Arguments: input SRV, output UAV, decoded words per record, first element.
// Decode fields explicitly: copying input bytes would not test HLSL layout.
[numthreads(1, 1, 1)]
void CS(uint3 thread : SV_DispatchThreadID) {
    StructuredBuffer<uint4> arguments = ResourceDescriptorHeap[g_ProbeArguments];
    uint4 args = arguments[0];
    RWByteAddressBuffer output = ResourceDescriptorHeap[args.y];
    uint element = args.w + thread.x;
    uint address = thread.x * args.z * 4;
    if (g_RecordKind == 0) {
        StructuredBuffer<ClusterLightRange> inputs = ResourceDescriptorHeap[args.x];
        ClusterLightRange value = inputs[element];
        output.Store2(address, uint2(value.offset, value.count));
    } else if (g_RecordKind == 1) {
        StructuredBuffer<LightGridBuildStatus> inputs = ResourceDescriptorHeap[args.x];
        LightGridBuildStatus value = inputs[element];
        output.Store4(address, uint4(value.state, value.reason,
            value.written_index_count, value.fallback_cell_count));
        output.Store2(address + 16, value.required_index_count);
        output.Store2(address + 24, value.selection_revision);
    } else if (g_RecordKind == 2) {
        StructuredBuffer<LightGridPassConstants> inputs = ResourceDescriptorHeap[args.x];
        LightGridPassConstants value = inputs[element];
        output.Store4(address, uint4(value.lighting_bindings_srv, value.ranges_uav,
            value.indices_uav, value.status_uav));
        output.Store4(address + 16, uint4(value.counts_uav, value.offsets_uav,
            value.subpass, value.work_count));
        output.Store2(address + 32, value.work_offset);
        output.Store2(address + 40, uint2(value.scan_stride, value.scan_phase));
    } else if (g_RecordKind == 3) {
        StructuredBuffer<LightShadowReference> inputs = ResourceDescriptorHeap[args.x];
        LightShadowReference value = inputs[element];
        output.Store4(address, uint4(value.projection_kind, value.record_index,
            value.selection_index, value.coverage_state));
    } else if (g_RecordKind == 4) {
        StructuredBuffer<DirectionalShadowRecord> inputs = ResourceDescriptorHeap[args.x];
        DirectionalShadowRecord value = inputs[element];
        output.Store4(address, uint4(value.selection_index, value.first_cascade,
            value.cascade_count, value.reserved));
    } else if (g_RecordKind == 5) {
        StructuredBuffer<LightGridMetadata> inputs = ResourceDescriptorHeap[args.x];
        LightGridMetadata value = inputs[element];
        output.Store4(address, uint4(value.grid_size, value.pixel_size_shift));
        output.Store2(address + 16, asuint(value.content_origin_px));
        output.Store2(address + 24, asuint(value.content_extent_px));
        output.Store4(address + 32, asuint(float4(value.grid_z_params, value.far_depth_m)));
        output.Store4(address + 48, uint4(asuint(value.near_depth_m), value.projection_kind,
            value.reserved));
    }
}
