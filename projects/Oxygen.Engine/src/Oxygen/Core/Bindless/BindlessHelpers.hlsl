//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_BINDLESS_HELPERS_HLSL
#define OXYGEN_BINDLESS_HELPERS_HLSL

// Usage: Shaders include only this file. This header includes the generated
// bindless layout file and provides policy-level helpers.
//
// Quick start (SM 6.6 descriptor heaps)
// -------------------------------------
//   // Define USE_SM66_BINDLESS if you want BX_SRV/BX_SAMPLER shortcuts.
//   #include "BindlessHelpers.hlsl"
//
//   // These "*_slot" values are global SRV heap indices supplied by the engine
//   // (e.g., via scene/draw constants or root constants). They point to buffers in
//   // the ResourceDescriptorHeap.
//   uint draw_metadata_buffer_slot   = /* engine-provided SRV slot for DrawMetadata buffer */ 0;
//   uint transforms_buffer_slot      = /* engine-provided SRV slot for world matrices   */ 0;
//
//   DrawMetadata meta;
//   if (!BX_LoadDrawMetadata(draw_metadata_buffer_slot, g_DrawIndex, meta)) {
//       // Drop the draw: required data is missing.
//   }
//   uint vIdx = BX_ResolveVertexIndex(meta, SV_VertexID);
//   BX_VERTEX_TYPE v = BX_LoadVertex(meta.vertex_buffer_index, vIdx);
//   float4x4 world;
//   if (!BX_TryLoadMatrix(transforms_buffer_slot, meta.transform_index, world)) {
//       // Drop the draw.
//   }
//
//   // Sample a texture via SM 6.6 descriptor heap (example)
//   // Indices provided by the engine are already global within the descriptor table.
//   uint texIdx  = /* global texture index (e.g., mat.base_color_texture_index) */ 0;
//   uint sampIdx = /* global sampler index */ 0;
//   Texture2D<float4> tex = BX_SRV(texIdx);
//   float4 color = tex.Sample(BX_SAMPLER(sampIdx), float2(0.5, 0.5));
//
// Customizing types
// -----------------
//   // Before including this file, you can override the following to match your structs:
//   // #define BX_VERTEX_TYPE    MyVertex
//   // #define BX_INDEX_TYPE     uint
//   // #include "BindlessHelpers.hlsl"
//
// Classic typed arrays (non-SM 6.6)
// ---------------------------------
//   // If you use classic register-based arrays, declare them in your shader and
//   // use the provided global indices directly:
//   // Texture2D<float4> g_Textures[] : register(t2, space0);
//   // SamplerState      g_Samplers[] : register(s0, space0);
//   // uint texIdx = /* global texture index */;
//   // uint smpIdx = /* global sampler index */;
//   // float4 c = g_Textures[texIdx].Sample(g_Samplers[smpIdx], uv);

// Notes
// -----
// - For optional validation in debug, you can use BX_IN_TEXTURES(texIdx) or
//   BX_TRY_TEXTURES(texIdx) from the generated layout to check/guard indices.
// - Global indices are absolute—no per-domain base offsets are needed by shaders.

#include "Generated.BindlessAbi.hlsl"

// Generic helpers derived from the generated layout
static inline bool BX_IsValid(uint idx) { return idx != K_INVALID_BINDLESS_INDEX; }

// ----------------------------------------------------------------------------
// Type knobs (override in shaders before including this header if needed)
// ----------------------------------------------------------------------------
#ifndef BX_VERTEX_TYPE
#define BX_VERTEX_TYPE Vertex
#endif
#ifndef BX_INDEX_TYPE
#define BX_INDEX_TYPE uint
#endif

// ----------------------------------------------------------------------------
// SM 6.6 descriptor-heap helpers (no declarations needed in RS)
// ----------------------------------------------------------------------------

// Quick validity check for a heap slot
static inline bool BX_IsValidSlot(uint slot) { return BX_IsValid(slot); }

// Whether a global-SRV slot is present and inside the global SRV domain.
static inline bool BX_IsGlobalSrvSlot(uint slot)
{
    return BX_IsValidSlot(slot) && BX_IN_GLOBAL_SRV(slot);
}

// Whether a slot is present and inside the materials domain, which holds
// material constants and the per-draw metadata, bounds and cull records.
static inline bool BX_IsMaterialsSlot(uint slot)
{
    return BX_IsValidSlot(slot) && BX_IN_MATERIALS(slot);
}

// Load the DrawMetadata of `drawIndex`; false when the slot is missing, outside
// the materials domain, or the index is out of bounds.
// Requires the DrawMetadata struct to be visible to the shader.
static inline bool BX_LoadDrawMetadata(uint slot, uint drawIndex, out DrawMetadata meta)
{
    meta = (DrawMetadata)0;
    if (!BX_IsMaterialsSlot(slot)) { return false; }
    StructuredBuffer<DrawMetadata> buf = ResourceDescriptorHeap[slot];
    uint count = 0u;
    uint stride = 0u;
    buf.GetDimensions(count, stride);
    if (drawIndex >= count) { return false; }
    meta = buf[drawIndex];
    return true;
}

// Resolve actual vertex index for this draw and vertexID using metadata
static inline uint BX_ResolveVertexIndex(const DrawMetadata meta, uint vertexID)
{
    if (meta.is_indexed != 0u) {
        Buffer<BX_INDEX_TYPE> ib = ResourceDescriptorHeap[meta.index_buffer_index];
        return (uint)(ib[meta.first_index + vertexID]) + (uint)meta.base_vertex;
    } else {
        return vertexID + (uint)meta.base_vertex;
    }
}

// Load a vertex from the vertex buffer referenced by metadata
static inline BX_VERTEX_TYPE BX_LoadVertex(uint vertexBufferIndex, uint vertexIndex)
{
    StructuredBuffer<BX_VERTEX_TYPE> vb = ResourceDescriptorHeap[vertexBufferIndex];
    return vb[vertexIndex];
}

// Load matrix `index` of a matrix buffer; false when the slot is missing,
// outside the global SRV domain, or the index is out of bounds. Callers drop
// the draw: there is no meaningful default transform.
static inline bool BX_TryLoadMatrix(uint slot, uint index, out float4x4 value)
{
    value = (float4x4)0;
    if (!BX_IsGlobalSrvSlot(slot)) { return false; }
    StructuredBuffer<float4x4> matrices = ResourceDescriptorHeap[slot];
    uint count = 0u;
    uint stride = 0u;
    matrices.GetDimensions(count, stride);
    if (index >= count) { return false; }
    value = matrices[index];
    return true;
}

// Resolve the transform index of one instance. Instanced draws
// (instance_count > 1) read it from the instance data buffer and fail without
// one; single-instance draws use meta.transform_index.
static inline bool BX_TryResolveTransformIndex(const DrawMetadata meta,
                                               uint instanceDataSlot,
                                               uint instanceID,
                                               out uint transformIndex)
{
    transformIndex = meta.transform_index;
    if (meta.instance_count <= 1u) { return true; }
    if (!BX_IsGlobalSrvSlot(instanceDataSlot)) { return false; }
    StructuredBuffer<uint> instance_data = ResourceDescriptorHeap[instanceDataSlot];
    uint count = 0u;
    uint stride = 0u;
    instance_data.GetDimensions(count, stride);
    const uint entry = meta.instance_metadata_offset + instanceID;
    if (entry >= count) { return false; }
    transformIndex = instance_data[entry];
    return true;
}

// Load the world matrix of one instance; false when it cannot be resolved.
static inline bool BX_TryLoadInstanceWorldMatrix(uint transformsSlot,
                                                 uint instanceDataSlot,
                                                 const DrawMetadata meta,
                                                 uint instanceID,
                                                 out float4x4 world)
{
    world = (float4x4)0;
    uint transform_index = 0u;
    return BX_TryResolveTransformIndex(meta, instanceDataSlot, instanceID, transform_index)
        && BX_TryLoadMatrix(transformsSlot, transform_index, world);
}

// Shader Model 6.6 bindless accessors (optional)
#if defined(USE_SM66_BINDLESS)
#define BX_SRV(absIndex)      ResourceDescriptorHeap[(absIndex)]
#define BX_SAMPLER(absIndex)  SamplerDescriptorHeap[(absIndex)]
#endif

#endif // OXYGEN_BINDLESS_HELPERS_HLSL
