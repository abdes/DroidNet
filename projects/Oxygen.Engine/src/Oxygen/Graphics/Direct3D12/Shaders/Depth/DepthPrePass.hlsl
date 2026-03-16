//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The entire heap is mapped to a single descriptor table covering t0-unbounded, space0.
// - Resources are intermixed in the heap (structured indirection + geometry buffers).
// - DrawMetadata structured buffer occupies a dynamic heap slot; its slot
//   is published each frame via DrawFrameBindings.
// - Uses ResourceDescriptorHeap for direct heap access with proper type casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DirectionalVirtualShadowMetadata.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/ShadowFrameBindings.hlsli"
#include "Renderer/VirtualShadowPageAccess.hlsli"

#include "Depth/DepthPrePassConstants.hlsli"

#include "MaterialFlags.hlsli"

#ifndef OXYGEN_VIRTUAL_SHADOW_RASTER
#define OXYGEN_VIRTUAL_SHADOW_RASTER 0
#endif

struct VertexData
{
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#define BX_VERTEX_TYPE VertexData
#include "Core/Bindless/BindlessHelpers.hlsl"

// Access to the bindless descriptor heap (SM 6.6+)
// No resource declarations needed - ResourceDescriptorHeap provides direct access

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : per-draw index into DrawMetadata
//   g_PassConstantsIndex : per-pass payload (typically a bindless index for
//                          pass-level constants)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

#if OXYGEN_VIRTUAL_SHADOW_RASTER
struct VirtualShadowRasterPassConstants
{
    float alpha_cutoff_default;
    uint schedule_srv_index;
    uint schedule_count_srv_index;
    uint atlas_tiles_per_axis;
    uint draw_page_ranges_srv_index;
    uint draw_page_indices_srv_index;
    uint _pad0;
    uint _pad1;
};

struct VirtualShadowResolvedScheduleEntry
{
    uint global_page_index;
    uint packed_entry;
    uint atlas_tile_x;
    uint atlas_tile_y;
};

struct VirtualShadowDrawPageRange
{
    uint offset;
    uint count;
    uint _pad0;
    uint _pad1;
};
#endif

// Output structure for the Vertex Shader
struct VS_OUTPUT_DEPTH {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
#if OXYGEN_VIRTUAL_SHADOW_RASTER
    float4 page_clip_distance : SV_ClipDistance0;
#endif
};

// -----------------------------------------------------------------------------
// Normal Matrix Integration Notes
// -----------------------------------------------------------------------------
// Depth pre-pass currently ignores vertex normals. With future features such as
// alpha-tested foliage or per-vertex displacement needing geometric normal
// adjustment, consider binding a normal matrix buffer parallel to world
// matrices. CPU now provides normal matrices as float4x4 (inverse-transpose of
// world upper 3x3) allowing direct StructuredBuffer<float4x4> consumption.
// Recommended steps for future integration:
//   1. Read `DrawFrameBindings.normal_matrices_slot` from DrawFrameBindings.
//   2. Fetch: `StructuredBuffer<float4x4> normals =
//         ResourceDescriptorHeap[draw_bindings.normal_matrices_slot];`
//   3. Derive normal3x3: `(float3x3)normals[meta.transform_index];`
//   4. Use for any operations needing correct normal orientation (e.g., alpha
//      test using normal mapped geometry or conservative depth adjustments).
// Avoid recomputing inverse/transpose in shader to save ALU; rely on CPU cache.
// Until such features are introduced, no shader changes are required.

#if OXYGEN_VIRTUAL_SHADOW_RASTER
static uint SelectDirectionalVirtualFilterRadiusTexels(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index)
{
    const float base_page_world =
        max(metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4);
    const float clip_page_world =
        max(metadata.clip_metadata[clip_index].origin_page_scale.z, base_page_world);
    const float texel_ratio = clip_page_world / base_page_world;
    return texel_ratio > 2.5 ? 2u : 1u;
}

static uint ResolveDirectionalVirtualGuardTexels(
    uint page_size_texels,
    uint filter_radius_texels)
{
    const uint max_guard_texels = max(1u, page_size_texels / 4u);
    return min(max_guard_texels, max(1u, filter_radius_texels));
}
#endif


// Vertex Shader: transforms vertices to clip space for depth buffer population.
[shader("vertex")]
VS_OUTPUT_DEPTH VS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT_DEPTH output;
    output.position = float4(0, 0, 0, 1);
    output.uv = float2(0, 0);
#if OXYGEN_VIRTUAL_SHADOW_RASTER
    output.page_clip_distance = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();

    DrawMetadata meta;
    if (!BX_LoadDrawMetadata(draw_bindings.draw_metadata_slot, g_DrawIndex, meta)) {
        return output;
    }

    const uint actual_vertex_index = BX_ResolveVertexIndex(meta, vertexID);
    const VertexData v = BX_LoadVertex(meta.vertex_buffer_index, actual_vertex_index);
    output.uv = v.texcoord;

#if OXYGEN_VIRTUAL_SHADOW_RASTER
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return output;
    }

    ConstantBuffer<VirtualShadowRasterPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass_constants.schedule_srv_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.schedule_count_srv_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.atlas_tiles_per_axis == 0u) {
        return output;
    }

    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const ShadowFrameBindings shadow_bindings =
        LoadShadowFrameBindings(view_bindings.shadow_frame_slot);
    if (shadow_bindings.virtual_directional_shadow_metadata_slot
        == K_INVALID_BINDLESS_INDEX) {
        return output;
    }

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
    const DirectionalVirtualShadowMetadata metadata = metadata_buffer[0];
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u) {
        return output;
    }

    StructuredBuffer<VirtualShadowResolvedScheduleEntry> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_srv_index];
    StructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_srv_index];
    const uint scheduled_page_count = schedule_count[0];

    const uint geometry_instance_count = max(meta.instance_count, 1u);
    uint page_instance_index = instanceID / geometry_instance_count;
    const uint geometry_instance_index = instanceID % geometry_instance_count;
    uint global_page_index = 0u;
    VirtualShadowResolvedScheduleEntry entry;
    if (pass_constants.draw_page_ranges_srv_index != K_INVALID_BINDLESS_INDEX
        && pass_constants.draw_page_indices_srv_index != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<VirtualShadowDrawPageRange> draw_page_ranges =
            ResourceDescriptorHeap[pass_constants.draw_page_ranges_srv_index];
        StructuredBuffer<uint> draw_page_indices =
            ResourceDescriptorHeap[pass_constants.draw_page_indices_srv_index];
        const VirtualShadowDrawPageRange draw_page_range = draw_page_ranges[g_DrawIndex];
        if (page_instance_index >= draw_page_range.count) {
            return output;
        }
        global_page_index = draw_page_indices[draw_page_range.offset + page_instance_index];

        if (shadow_bindings.virtual_shadow_page_table_slot == K_INVALID_BINDLESS_INDEX) {
            return output;
        }
        StructuredBuffer<uint> page_table =
            ResourceDescriptorHeap[shadow_bindings.virtual_shadow_page_table_slot];
        const uint page_table_index = metadata.page_table_offset + global_page_index;
        uint page_table_count = 0u;
        uint page_table_stride = 0u;
        page_table.GetDimensions(page_table_count, page_table_stride);
        if (page_table_index >= page_table_count) {
            return output;
        }

        const VirtualShadowPageTableEntry page_entry =
            DecodeVirtualShadowPageTableEntry(page_table[page_table_index]);
        if (!VirtualShadowPageTableEntryHasCurrentLod(page_entry)) {
            return output;
        }
        entry.global_page_index = global_page_index;
        entry.packed_entry = page_table[page_table_index];
        entry.atlas_tile_x = page_entry.tile_x;
        entry.atlas_tile_y = page_entry.tile_y;
    } else if (page_instance_index >= scheduled_page_count) {
        return output;
    } else {
        entry = schedule[page_instance_index];
        global_page_index = entry.global_page_index;
    }
    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    if (pages_per_level == 0u) {
        return output;
    }
    const uint clip_index = global_page_index / pages_per_level;
    const uint local_page_index = global_page_index % pages_per_level;
    if (clip_index >= metadata.clip_level_count) {
        return output;
    }

    const uint page_x = local_page_index % metadata.pages_per_axis;
    const uint page_y = local_page_index / metadata.pages_per_axis;
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const uint filter_guard_texels = ResolveDirectionalVirtualGuardTexels(
        metadata.page_size_texels,
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index));
    const float interior_texels = max(
        1.0,
        float(metadata.page_size_texels) - float(filter_guard_texels * 2u));
    const float page_guard_world =
        page_world_size * (float(filter_guard_texels) / interior_texels);
    const float logical_left = clip.origin_page_scale.x + float(page_x) * page_world_size;
    const float logical_right = logical_left + page_world_size;
    const float logical_bottom = clip.origin_page_scale.y + float(page_y) * page_world_size;
    const float logical_top = logical_bottom + page_world_size;
    const float left = logical_left - page_guard_world;
    const float right = logical_right + page_guard_world;
    const float bottom = logical_bottom - page_guard_world;
    const float top = logical_top + page_guard_world;

    // Use per-instance transform (handles GPU instancing automatically)
    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.transforms_slot,
        draw_bindings.instance_data_slot,
        meta,
        geometry_instance_index);
    const float4 world_pos = mul(world_matrix, float4(v.position, 1.0f));
    const float4 local_view_pos = mul(metadata.light_view, world_pos);
    const float2 clip_extent = float2(
        max(right - left, 1.0e-4),
        max(top - bottom, 1.0e-4));
    float4 local_clip_pos;
    local_clip_pos.x = (2.0 * local_view_pos.x - (right + left)) / clip_extent.x;
    local_clip_pos.y = (2.0 * local_view_pos.y - (top + bottom)) / clip_extent.y;
    local_clip_pos.z =
        local_view_pos.z * clip.origin_page_scale.w + clip.bias_reserved.x;
    local_clip_pos.w = 1.0;

    // Preserve the original page-local clip planes before remapping into the
    // atlas-wide clip space. Without this, geometry outside the page frustum
    // can spill into neighboring atlas tiles.
    output.page_clip_distance = float4(
        local_clip_pos.x + local_clip_pos.w,
        local_clip_pos.w - local_clip_pos.x,
        local_clip_pos.y + local_clip_pos.w,
        local_clip_pos.w - local_clip_pos.y);

    const float atlas_scale = 1.0 / float(pass_constants.atlas_tiles_per_axis);
    const float2 atlas_ndc_scale_bias = float2(
        (float(entry.atlas_tile_x) * 2.0 + 1.0) * atlas_scale - 1.0,
        1.0 - (float(entry.atlas_tile_y) * 2.0 + 1.0) * atlas_scale);
    const float2 local_ndc = local_clip_pos.xy;
    const float2 atlas_ndc =
        local_ndc * atlas_scale + atlas_ndc_scale_bias;
    output.position = float4(
        atlas_ndc,
        local_clip_pos.z,
        1.0);
#else
    // Use per-instance transform (handles GPU instancing automatically)
    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.transforms_slot, draw_bindings.instance_data_slot, meta, instanceID);
    const float4 world_pos = mul(world_matrix, float4(v.position, 1.0f));
    const float4 view_pos = mul(view_matrix, world_pos);
    output.position = mul(projection_matrix, view_pos);
#endif
    return output;
}

// Pixel Shader: depth-only pass with optional alpha-test.
// When ALPHA_TEST is defined, performs alpha-test clip for masked materials.
// When ALPHA_TEST is not defined, this is a no-op (opaque depth path).
[shader("pixel")]
void PS(VS_OUTPUT_DEPTH input)
{
#ifdef ALPHA_TEST
    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
    if (!BX_IsValidSlot(draw_bindings.draw_metadata_slot) ||
        !BX_IsValidSlot(draw_bindings.material_shading_constants_slot)) {
        return;
    }

    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[draw_bindings.draw_metadata_slot];
    const DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    StructuredBuffer<MaterialShadingConstants> materials = ResourceDescriptorHeap[draw_bindings.material_shading_constants_slot];
    const MaterialShadingConstants mat = materials[meta.material_handle];

    const bool alpha_test_enabled = (mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
    if (!alpha_test_enabled) {
        return;
    }

    float alpha = mat.base_color.a;

    const bool no_texture_sampling =
        (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

    // Note: Depth pre-pass relies entirely on the base color texture's alpha for cutouts,
    // satisfying masked material contracts without needing the heavy PBR evaluations.
    if (!no_texture_sampling && mat.base_color_texture_index != K_INVALID_BINDLESS_INDEX) {
        const float2 uv = ApplyMaterialUv(input.uv, mat);
        Texture2D<float4> base_tex = ResourceDescriptorHeap[mat.base_color_texture_index];
        SamplerState samp = SamplerDescriptorHeap[0];
        const float4 texel = base_tex.Sample(samp, uv);
        alpha *= texel.a;
    }

    float cutoff = mat.alpha_cutoff;
    if (cutoff <= 0.0f && g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX) {
#if OXYGEN_VIRTUAL_SHADOW_RASTER
        ConstantBuffer<VirtualShadowRasterPassConstants> pass_constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
#else
        ConstantBuffer<DepthPrePassConstants> pass_constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
#endif
        cutoff = pass_constants.alpha_cutoff_default;
    }

    clip(alpha - cutoff);
#else
    (void)input;
#endif
}
