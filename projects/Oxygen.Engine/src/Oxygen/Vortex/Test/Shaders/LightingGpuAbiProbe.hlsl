//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Lighting/LightGridData.hlsli"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Contracts/Lighting/DeferredLightConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Contracts/Shadows/ShadowRecords.hlsli"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"

struct ProbeArguments {
    uint4 decode;
    uint indices_srv;
    uint3 reserved;
};

struct GridLookupProbeInput {
    LightGridMetadata grid;
    float2 screen_position;
    float view_depth;
    uint reserved;
};

struct LightIterationProbeInput {
    ClusterLightRange range;
    uint local_count;
    uint reserved;
};

cbuffer ProbeRoot : register(b2, space0) {
    uint g_RecordKind;
    uint g_ProbeArguments;
};

// Arguments: input SRV, output UAV, decoded words per record, first element.
// Decode fields explicitly: copying input bytes would not test HLSL layout.
[numthreads(1, 1, 1)]
void CS(uint3 thread : SV_DispatchThreadID) {
    StructuredBuffer<ProbeArguments> arguments = ResourceDescriptorHeap[g_ProbeArguments];
    uint4 args = arguments[0].decode;
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
    } else if (g_RecordKind == 6) {
        StructuredBuffer<GridLookupProbeInput> inputs = ResourceDescriptorHeap[args.x];
        GridLookupProbeInput value = inputs[element];
        output.Store(address, ComputeClusterIndex(value.screen_position, value.view_depth, value.grid));
    } else if (g_RecordKind == 7) {
        StructuredBuffer<LightIterationProbeInput> inputs = ResourceDescriptorHeap[args.x];
        LightIterationProbeInput value = inputs[element];
        ClusterLightIteration iteration;
        bool valid = TryResolveClusterLightIteration(value.range, value.local_count,
            arguments[0].indices_srv, iteration);
        uint sum = 0u, low_mask = 0u, high_mask = 0u;
        if (valid) {
            for (uint i = 0u; i < iteration.count; ++i) {
                uint index = LoadClusterLightIndex(iteration, i);
                sum += index;
                if (index < 32u) low_mask |= 1u << index;
                else if (index < 64u) high_mask |= 1u << (index - 32u);
            }
        }
        output.Store4(address, uint4(valid, iteration.count, sum, low_mask));
        output.Store(address + 16, high_mask);
    } else if (g_RecordKind == 8) {
        StructuredBuffer<ForwardLocalLightRecord> inputs = ResourceDescriptorHeap[args.x];
        ForwardLocalLightRecord value = inputs[element];
        output.Store4(address, asuint(float4(value.position_ws, value.range_m)));
        output.Store4(address + 16, asuint(float4(value.intensity_rgb_cd, value.source_radius_m)));
        output.Store4(address + 32, asuint(float4(value.emitted_direction_ws, value.inverse_range_m)));
        output.Store4(address + 48, uint4(asuint(value.inner_cone_sin_half_squared),
            asuint(value.outer_cone_sin_half_squared), value.kind, value.flags));
        output.Store4(address + 64, uint4(value.selection_index, value.reserved));
    } else if (g_RecordKind == 9) {
        StructuredBuffer<DirectionalLightForwardData> inputs = ResourceDescriptorHeap[args.x];
        DirectionalLightForwardData value = inputs[element];
        output.Store4(address, uint4(asuint(value.direction_to_source_ws), value.atmosphere_light_slot));
        output.Store4(address + 16, uint4(asuint(value.illuminance_rgb_lux), value.flags));
        output.Store4(address + 32, uint4(asuint(value.ground_transmittance_rgb), value.atmosphere_mode_flags));
        output.Store4(address + 48, uint4(value.selection_index, value.reserved));
    } else if (g_RecordKind == 10) {
        StructuredBuffer<LightingFrameBindings> inputs = ResourceDescriptorHeap[args.x];
        LightingFrameBindings value = inputs[element];
        output.Store4(address, uint4(value.directional_records_srv, value.local_records_srv,
            value.cluster_ranges_srv, value.local_indices_srv));
        output.Store4(address + 16, uint4(value.directional_count, value.local_count,
            value.cluster_count, value.index_capacity));
        output.Store4(address + 32, uint4(value.directional_shadow_map_srv, value.local_shadow_map_srv,
            value.build_status_srv, value.grid_metadata_srv));
        output.Store4(address + 48, uint4(value.scene_generation, value.selection_revision));
        output.Store4(address + 64, uint4(value.frame_sequence, value.view_generation));
        output.Store4(address + 80, uint4(value.publication_state, value.brdf_moments_srv,
            value.brdf_mean_moments_srv, value.brdf_model_revision));
    } else if (g_RecordKind == 11) {
        StructuredBuffer<uint> cbv_slots = ResourceDescriptorHeap[args.x];
        ConstantBuffer<DeferredLightConstants> value = ResourceDescriptorHeap[cbv_slots[element]];
        // Matrix-vector multiplication independently proves upload orientation.
        output.Store4(address, asuint(mul(value.light_world_matrix, float4(1, 2, 4, 8))));
        output.Store4(address + 16, uint4(value.light_type, value.selection_index,
            value.light_geometry_vertices_srv, value.light_geometry_vertex_count));
    } else if (g_RecordKind == 12) {
        StructuredBuffer<ViewFrameBindings> inputs = ResourceDescriptorHeap[args.x];
        ViewFrameBindings value = inputs[element];
        output.Store4(address, uint4(value.draw_frame_slot, value.lighting_frame_slot,
            value.environment_frame_slot, value.frame_exposure_slot));
        output.Store4(address + 16, uint4(value.scene_texture_frame_slot, value.scene_depth_slot,
            value.screen_hzb_frame_slot, value.shadow_frame_slot));
        output.Store4(address + 32, uint4(value.virtual_shadow_frame_slot, value.post_process_frame_slot,
            value.debug_frame_slot, value.history_frame_slot));
        output.Store4(address + 48, uint4(value.ray_tracing_frame_slot, value.exposure_status_uav,
            value.lighting_view_generation));
    } else if (g_RecordKind == 13) {
        StructuredBuffer<VortexShadowCascadeBinding> inputs = ResourceDescriptorHeap[args.x];
        VortexShadowCascadeBinding value = inputs[element];
        // Transform basis vectors to verify every matrix lane and orientation.
        output.Store4(address, asuint(mul(value.light_view_projection, float4(1, 0, 0, 0))));
        output.Store4(address + 16, asuint(mul(value.light_view_projection, float4(0, 1, 0, 0))));
        output.Store4(address + 32, asuint(mul(value.light_view_projection, float4(0, 0, 1, 0))));
        output.Store4(address + 48, asuint(mul(value.light_view_projection, float4(0, 0, 0, 1))));
        output.Store4(address + 64, asuint(float4(value.split_near, value.split_far,
            value.depth_bias, value.normal_bias_m)));
        output.Store4(address + 80, uint4(value.surface_srv, value.array_layer, value.reserved0));
        output.Store4(address + 96, asuint(float4(value.inverse_resolution,
            value.world_texel_size, value.transition_width)));
        output.Store4(address + 112, uint4(asuint(value.fade_begin), asuint(value.fade_end), value.reserved1));
    }
}
