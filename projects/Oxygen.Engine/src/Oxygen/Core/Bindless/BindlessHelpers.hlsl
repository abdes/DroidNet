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
//   uint materials_buffer_slot       = /* engine-provided SRV slot for materials       */ 0;
//
//   DrawMetadata meta;
//   if (!BX_LoadDrawMetadata(draw_metadata_buffer_slot, g_DrawIndex, meta)) {
//       // Fallback path (e.g., skip draw or use defaults)
//   }
//   uint vIdx = BX_ResolveVertexIndex(meta, SV_VertexID);
//   BX_VERTEX_TYPE v = BX_LoadVertex(meta.vertex_buffer_index, vIdx);
//   float4x4 world = BX_LoadWorldMatrix(transforms_buffer_slot, meta.transform_index);
//   BX_MATERIAL_TYPE mat = BX_LoadMaterial(materials_buffer_slot, meta.material_handle);
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
//   // #define BX_MATERIAL_TYPE  MyMaterialConstants
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

#include "Generated.BindlessLayout.hlsl"

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
#ifndef BX_MATERIAL_TYPE
#define BX_MATERIAL_TYPE MaterialConstants
#endif

// ----------------------------------------------------------------------------
// SM 6.6 descriptor-heap helpers (no declarations needed in RS)
// ----------------------------------------------------------------------------

// Quick validity check for a heap slot
static inline bool BX_IsValidSlot(uint slot) { return BX_IsValid(slot); }

// Load DrawMetadata for current draw; returns false if slot is invalid.
// Requires the DrawMetadata struct to be visible to the shader.
static inline bool BX_LoadDrawMetadata(uint slot, uint drawIndex, out DrawMetadata meta)
{
    if (!BX_IsValidSlot(slot)) { meta = (DrawMetadata)0; return false; }
    StructuredBuffer<DrawMetadata> buf = ResourceDescriptorHeap[slot];
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

// Load a world matrix from a transforms buffer slot (returns identity if invalid)
static inline float4x4 BX_LoadWorldMatrix(uint transformsSlot, uint transformIndex)
{
    if (BX_IsValidSlot(transformsSlot)) {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[transformsSlot];
        return worlds[transformIndex];
    }
    // Identity
    return float4x4(1,0,0,0,
                    0,1,0,0,
                    0,0,1,0,
                    0,0,0,1);
}

// Resolve per-instance transform index for GPU instancing.
// For instanced draws (instance_count > 1), fetches from instance data buffer.
// For single-instance draws, returns meta.transform_index directly.
static inline uint BX_ResolveTransformIndex(const DrawMetadata meta,
                                            uint instanceDataSlot,
                                            uint instanceID)
{
    if (meta.instance_count > 1 && BX_IsValidSlot(instanceDataSlot)) {
        StructuredBuffer<uint> instance_data = ResourceDescriptorHeap[instanceDataSlot];
        return instance_data[meta.instance_metadata_offset + instanceID];
    }
    return meta.transform_index;
}

// Load a world matrix for a specific instance (handles GPU instancing).
static inline float4x4 BX_LoadInstanceWorldMatrix(uint transformsSlot,
                                                   uint instanceDataSlot,
                                                   const DrawMetadata meta,
                                                   uint instanceID)
{
    const uint transformIndex = BX_ResolveTransformIndex(meta, instanceDataSlot, instanceID);
    return BX_LoadWorldMatrix(transformsSlot, transformIndex);
}

// Load a material constants record from a materials buffer slot
static inline BX_MATERIAL_TYPE BX_LoadMaterial(uint materialsSlot, uint materialIndex)
{
    StructuredBuffer<BX_MATERIAL_TYPE> mats = ResourceDescriptorHeap[materialsSlot];
    return mats[materialIndex];
}

// Shader Model 6.6 bindless accessors (optional)
#if defined(USE_SM66_BINDLESS)
#define BX_SRV(absIndex)      ResourceDescriptorHeap[(absIndex)]
#define BX_SAMPLER(absIndex)  SamplerDescriptorHeap[(absIndex)]
#endif

#endif // OXYGEN_BINDLESS_HELPERS_HLSL
