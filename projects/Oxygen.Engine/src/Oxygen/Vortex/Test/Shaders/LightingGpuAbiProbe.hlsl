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
#include "Vortex/Contracts/Shadows/ShadowFrameBindings.hlsli"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"
#include "Vortex/Contracts/Shadows/ProjectedLocalShadowRecord.hlsli"
#include "Vortex/Contracts/Shadows/CubeLocalShadowRecord.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"
#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
#include "Vortex/Services/Lighting/FiniteEmitter.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"
#include "Vortex/Services/Lighting/LocalLightAttenuation.hlsli"
#include "Vortex/Services/Lighting/DeferredShadingCommon.hlsli"
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"

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

struct MaterialDecodeProbeInput {
    uint normal_srv;
    uint material_srv;
    uint base_color_srv;
    uint pixel_x;
};

struct PhotometryProbeInput {
    float3 light_vector;
    float range_m;
    float3 direction_to_source;
    float inner_sin_half_squared;
    float3 emitted_axis;
    float outer_sin_half_squared;
    float3 intensity_rgb_cd;
    uint is_spot;
    float inner_relative_correction;
    float outer_relative_correction;
};

struct BrdfProbeInput {
    float roughness;
    float light_cosine;
    float view_cosine;
    float azimuth;
    float3 base_color;
    float metallic;
    float3 incident_rgb;
    float specular;
    uint moments_srv;
    uint means_srv;
};

struct FurnaceProbeInput {
    float roughness;
    float view_cosine;
    float azimuth;
    float light_cosine;
    uint order;
    uint moments_srv;
    uint means_srv;
};

struct FiniteEmitterProbeInput {
    ForwardLocalLightRecord light;
    float3 view;
    float roughness;
    uint moments_srv;
    uint means_srv;
    float f0;
    float rho;
};

struct MomentProbeInput {
    float roughness;
    float view_cosine;
    uint moments_srv;
    uint means_srv;
};

struct MaterialUvProbeInput {
    float2 uv;
    float2 scale;
    float2 offset;
    float rotation_radians;
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
        output.Store4(address + 64, uint4(value.selection_index,
            asuint(value.inner_cone_relative_correction), asuint(value.outer_cone_relative_correction), value.reserved));
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
    } else if (g_RecordKind == 14) {
        StructuredBuffer<ProjectedLocalShadowRecord> inputs = ResourceDescriptorHeap[args.x];
        ProjectedLocalShadowRecord value = inputs[element];
        [unroll] for (uint column = 0; column < 4; ++column) {
            float4 basis = 0;
            basis[column] = 1;
            output.Store4(address + column * 16, asuint(mul(value.light_view_projection, basis)));
        }
        output.Store4(address + 64, asuint(float4(value.shadow_origin_ws, value.near_plane_m)));
        output.Store4(address + 80, asuint(float4(value.far_plane_m, value.normal_bias_m,
            value.depth_bias, value.world_texel_size)));
        output.Store4(address + 96, uint4(value.surface_srv, value.array_layer,
            value.selection_index, value.reserved0));
        output.Store4(address + 112, uint4(asuint(value.inverse_resolution), value.reserved1));
    } else if (g_RecordKind == 15) {
        StructuredBuffer<CubeLocalShadowRecord> inputs = ResourceDescriptorHeap[args.x];
        CubeLocalShadowRecord value = inputs[element];
        [unroll] for (uint face = 0; face < 6; ++face) {
            [unroll] for (uint column = 0; column < 4; ++column) {
                float4 basis = 0;
                basis[column] = 1;
                output.Store4(address + face * 64 + column * 16,
                    asuint(mul(value.face_light_view_projection[face], basis)));
            }
        }
        output.Store4(address + 384, asuint(float4(value.shadow_origin_ws, value.near_plane_m)));
        output.Store4(address + 400, asuint(float4(value.far_plane_m, value.normal_bias_m,
            value.depth_bias, value.world_texel_size)));
        output.Store4(address + 416, uint4(value.surface_srv, value.first_array_layer,
            value.selection_index, value.reserved0));
        output.Store4(address + 432, uint4(asuint(value.inverse_resolution), value.reserved1));
    } else if (g_RecordKind == 16) {
        StructuredBuffer<VortexShadowFrameBindings> inputs = ResourceDescriptorHeap[args.x];
        VortexShadowFrameBindings value = inputs[element];
        output.Store4(address, uint4(value.directional_records_srv, value.directional_record_count,
            value.projected_local_records_srv, value.projected_local_record_count));
        output.Store4(address + 16, uint4(value.cube_local_records_srv, value.cube_local_record_count,
            value.cascade_records_srv, value.cascade_record_count));
        output.Store4(address + 32, uint4(value.contact_depth_srv, value.view_status_srv,
            value.contact_enabled, value.sampling_flags));
        output.Store4(address + 48, asuint(float4(value.contact_content_origin_px, value.contact_content_extent_px)));
        output.Store4(address + 64, uint4(value.scene_generation, value.selection_revision));
        output.Store4(address + 80, uint4(value.frame_sequence, value.view_generation));
        output.Store4(address + 96, uint4(value.contact_texture_extent_px, value.reserved));
    } else if (g_RecordKind == 17) {
        StructuredBuffer<MaterialDecodeProbeInput> inputs = ResourceDescriptorHeap[args.x];
        MaterialDecodeProbeInput value = inputs[element];
        Texture2D<float4> normals = ResourceDescriptorHeap[value.normal_srv];
        Texture2D<float4> materials = ResourceDescriptorHeap[value.material_srv];
        Texture2D<float4> colors = ResourceDescriptorHeap[value.base_color_srv];
        int3 location = int3(value.pixel_x, 0, 0);
        float3 normal = DecodeGBufferNormal(normals.Load(location));
        float metallic, specular, roughness, ao;
        uint model;
        DecodeGBufferMaterial(materials.Load(location), metallic, specular, roughness, model);
        float3 base_color;
        DecodeGBufferBaseColor(colors.Load(location), base_color, ao);
        float3 f0 = ComputeMetallicF0(base_color, metallic, specular);
        float3 diffuse = base_color * (1.0 - metallic);
        output.Store4(address, asuint(float4(normal, metallic)));
        output.Store4(address + 16, uint4(asuint(float3(specular, roughness, ao)), model));
        output.Store4(address + 32, asuint(float4(base_color, 0.0)));
        output.Store4(address + 48, asuint(float4(f0, 0.0)));
        output.Store4(address + 64, asuint(float4(diffuse, 0.0)));
    } else if (g_RecordKind == 18) {
        StructuredBuffer<PhotometryProbeInput> inputs = ResourceDescriptorHeap[args.x];
        PhotometryProbeInput value = inputs[element];
        float distance_factor = ComputeLocalLightDistanceAttenuation(value.light_vector, value.range_m);
        float angular_factor = value.is_spot != 0
            ? ComputeSpotLightAngularAttenuation(value.direction_to_source, value.emitted_axis,
                float2(value.inner_sin_half_squared, value.inner_relative_correction),
                float2(value.outer_sin_half_squared, value.outer_relative_correction))
            : 1.0;
        float3 normal_illuminance = value.intensity_rgb_cd * distance_factor * angular_factor;
        output.Store2(address, asuint(float2(distance_factor, angular_factor)));
        output.Store3(address + 8, asuint(normal_illuminance));
    } else if (g_RecordKind == 19) {
        StructuredBuffer<BrdfProbeInput> inputs = ResourceDescriptorHeap[args.x];
        BrdfProbeInput value = inputs[element];
        float light_sine = sqrt((1.0 - value.light_cosine) * (1.0 + value.light_cosine));
        float view_sine = sqrt((1.0 - value.view_cosine) * (1.0 + value.view_cosine));
        float3 L = float3(light_sine * cos(value.azimuth), light_sine * sin(value.azimuth), value.light_cosine);
        float3 V = float3(view_sine, 0.0, value.view_cosine);
        float3 H = normalize(L + V);
        float3 f0 = ComputeMetallicF0(value.base_color, value.metallic, value.specular);
        DeferredLightingSurfaceData surface = (DeferredLightingSurfaceData)0;
        surface.world_normal = float3(0.0, 0.0, 1.0);
        surface.view_direction = V;
        surface.base_color = value.base_color;
        surface.metallic = value.metallic;
        surface.specular = value.specular;
        surface.specular_f0 = f0;
        surface.roughness = max(value.roughness, kVortexDeferredMinRoughness);
        surface.ambient_occlusion = 1.0;
        LightingFrameBindings lighting = (LightingFrameBindings)0;
        lighting.brdf_model_revision = 1u;
        lighting.brdf_moments_srv = value.moments_srv;
        lighting.brdf_mean_moments_srv = value.means_srv;
        float3 deferred = EvaluateCookTorranceLighting(surface, L, value.incident_rgb, lighting);
        float3 forward = EvaluateGgxDirectResponse(surface.world_normal, V, L, f0,
            value.base_color * (1.0 - value.metallic), value.roughness, lighting)
            * value.incident_rgb;
        float distribution = GgxDistribution(surface.world_normal, H, value.roughness);
        output.Store4(address, asuint(float4(deferred, distribution)));
        output.Store4(address + 16, asuint(float4(forward, distribution)));
        output.Store4(address + 32, asuint(float4(f0, 0.0)));
    } else if (g_RecordKind == 23) {
        StructuredBuffer<BrdfProbeInput> inputs = ResourceDescriptorHeap[args.x];
        BrdfProbeInput value = inputs[element];
        float3 N = float3(0, 0, 1);
        float3 V = float3(sqrt(1.0 - value.view_cosine * value.view_cosine), 0, value.view_cosine);
        float ls = sqrt(1.0 - value.light_cosine * value.light_cosine);
        float3 L = float3(ls * cos(value.azimuth), ls * sin(value.azimuth), value.light_cosine);
        LightingFrameBindings lighting = (LightingFrameBindings)0;
        lighting.brdf_model_revision = 1u;
        lighting.brdf_moments_srv = value.moments_srv;
        lighting.brdf_mean_moments_srv = value.means_srv;
        float3 f0 = ComputeMetallicF0(value.base_color, value.metallic, value.specular);
        float3 rho = value.base_color * (1.0 - value.metallic);
        GgxDirectLobes a = EvaluateGgxDirectLobes(N, V, L, f0, rho, value.roughness, lighting);
        GgxDirectLobes b = EvaluateGgxDirectLobes(N, L, V, f0, rho, value.roughness, lighting);
        output.Store3(address, asuint(a.single_scattering / value.light_cosine));
        output.Store3(address + 12, asuint(a.multiple_scattering / value.light_cosine));
        output.Store3(address + 24, asuint(a.diffuse / value.light_cosine));
        output.Store3(address + 36, asuint(b.single_scattering / value.view_cosine));
        output.Store3(address + 48, asuint(b.multiple_scattering / value.view_cosine));
        output.Store3(address + 60, asuint(b.diffuse / value.view_cosine));
    } else if (g_RecordKind == 24) {
        StructuredBuffer<FurnaceProbeInput> inputs = ResourceDescriptorHeap[args.x];
        FurnaceProbeInput value = inputs[element];
        LightingFrameBindings lighting = (LightingFrameBindings)0;
        lighting.brdf_model_revision = 1u;
        lighting.brdf_moments_srv = value.moments_srv;
        lighting.brdf_mean_moments_srv = value.means_srv;
        const float3 F0 = float3(0.04, 0.45, 1.0);
        const float3 rho = float3(0.8, 0.25, 0.0);
        const float mu = value.view_cosine;
        const float vs = sqrt(1.0 - mu * mu);
        const float3 N = float3(0, 0, 1);
        const float3 V = float3(vs, 0, mu);
        const float alpha = value.roughness * value.roughness;
        const float cp = cos(value.azimuth);
        const float sp = sin(value.azimuth);
        const float projection = vs * cp;
        const float root = sqrt(projection * projection + mu * mu);
        const float maximum_tangent = projection >= 0.0
            ? (root + projection) / mu : mu / (root - projection);
        const float maximum_psi = atan(maximum_tangent / alpha);
        float3 single = 0.0.xxx;
        // Independent half-vector quadrature resolves the smooth NDF peak.
        // Each dispatch thread owns one azimuth; radial work stays bounded.
        for (uint radial = 0u; radial < value.order; ++radial) {
            float psi = (float(radial) + 0.5) * maximum_psi / float(value.order);
            float sn = sin(psi), cs = cos(psi);
            float denominator_squared = cs * cs + alpha * alpha * sn * sn;
            float inverse_denominator = rsqrt(denominator_squared);
            float sh = alpha * sn * inverse_denominator;
            float ch = cs * inverse_denominator;
            float3 H = float3(sh * cp, sh * sp, ch);
            float vh = dot(V, H);
            float3 L = 2.0 * vh * H - V;
            if (L.z > 0.0) {
                GgxDirectLobes lobes = EvaluateGgxDirectLobes(N, V, L,
                    F0, rho, value.roughness, lighting);
                float jacobian = 4.0 * vh * sh * alpha / denominator_squared;
                single += lobes.single_scattering * jacobian;
            }
        }
        single *= maximum_psi * 2.0 * PI / (float(value.order) * float(value.order));
        // Smooth compensation/diffuse lobes use a separate incoming-cosine rule.
        float nl = value.light_cosine;
        float3 L = float3(sqrt(1.0 - nl * nl), 0, nl);
        GgxDirectLobes smooth = EvaluateGgxDirectLobes(N, V, L,
            F0, rho, value.roughness, lighting);
        GgxIntegratedLobes integrated = EvaluateGgxIntegratedLobes(mu,
            F0, rho, value.roughness, lighting);
        output.Store3(address, asuint(single));
        output.Store3(address + 12, asuint(smooth.multiple_scattering * (2.0 * PI / float(value.order))));
        output.Store3(address + 24, asuint(smooth.diffuse * (2.0 * PI / float(value.order))));
        output.Store3(address + 36, asuint(integrated.specular));
        output.Store3(address + 48, asuint(integrated.diffuse));
    } else if (g_RecordKind == 25) {
        StructuredBuffer<FiniteEmitterProbeInput> inputs = ResourceDescriptorHeap[args.x];
        FiniteEmitterProbeInput value = inputs[element];
        LightingFrameBindings lighting = (LightingFrameBindings)0;
        lighting.brdf_model_revision = 1u;
        lighting.brdf_moments_srv = value.moments_srv;
        lighting.brdf_mean_moments_srv = value.means_srv;
        GgxDirectLobes result = EvaluateLocalEmitterLobes(value.light, 0.0.xxx,
            float3(0,0,1), value.view, value.f0.xxx, value.rho.xxx, value.roughness, lighting);
        output.Store3(address, asuint(float3(result.single_scattering.r,
            result.multiple_scattering.r, result.diffuse.r)));
    } else if (g_RecordKind == 22) {
        StructuredBuffer<MomentProbeInput> inputs = ResourceDescriptorHeap[args.x];
        MomentProbeInput value = inputs[element];
        LightingFrameBindings lighting = (LightingFrameBindings)0;
        lighting.brdf_model_revision = 1u;
        lighting.brdf_moments_srv = value.moments_srv;
        lighting.brdf_mean_moments_srv = value.means_srv;
        float3 N = float3(0.0, 0.0, 1.0);
        float3 L = normalize(float3(1.0, 0.0, value.view_cosine));
        float3 V = normalize(float3(-1.0, 0.0, value.view_cosine));
        GgxDirectLobes response = EvaluateGgxDirectLobes(N, V, L, 1.0.xxx, 0.0.xxx, value.roughness, lighting);
        float3 total = response.single_scattering + response.multiple_scattering + response.diffuse;
        float3 swapped = EvaluateGgxDirectResponse(N, L, V, 1.0.xxx, 0.0.xxx, value.roughness, lighting);
        output.Store4(address, asuint(float4(response.single_scattering.r, total.r, swapped.r, response.diffuse.r)));
    } else if (g_RecordKind == 21) {
        StructuredBuffer<MomentProbeInput> inputs = ResourceDescriptorHeap[args.x];
        MomentProbeInput value = inputs[element];
        float2 moment = SampleGgxMomentTexture(value.moments_srv, value.view_cosine, value.roughness);
        float2 mean = SampleGgxMomentTexture(value.means_srv, 0.0, value.roughness);
        output.Store4(address, asuint(float4(moment, mean)));
    } else if (g_RecordKind == 20) {
        StructuredBuffer<MaterialUvProbeInput> inputs = ResourceDescriptorHeap[args.x];
        MaterialUvProbeInput value = inputs[element];
        MaterialShadingConstants material = (MaterialShadingConstants)0;
        material.uv_scale = value.scale;
        material.uv_offset = value.offset;
        material.uv_rotation_radians = value.rotation_radians;
        output.Store2(address, asuint(ApplyMaterialUv(value.uv, material)));
    }
}
