//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::engine {

//! Per-draw metadata for future-proof bindless rendering.
/*!
 Comprehensive draw metadata that replaces the simple world transforms buffer
 approach. Contains indices into various binding buffers and draw configuration
 data for efficient GPU-driven rendering.

 ### Usage Notes

 - Populated on the CPU and uploaded to a structured buffer SRV.
 - Shaders read entries via a dynamic bindless slot provided in
   SceneConstants (see bindless_draw_metadata_slot).
 - The slot value may change per frame; do not assume a fixed slot.

 @see RenderableBinding
 @see SceneConstants
*/
struct alignas(16) DrawMetadata {
  uint32_t vertex_buffer_index; // Bindless index into vertex buffer table
  uint32_t index_buffer_index; // Bindless index into index buffer table
  uint32_t is_indexed; // 0 = non-indexed, 1 = indexed
  uint32_t instance_count; // Number of instances to draw

  uint32_t transform_offset; // Offset into transform buffer for this draw

  uint32_t material_index; // Index into material constants buffer for this draw

  uint32_t instance_metadata_buffer_index; // Bindless index into instance
                                           // metadata buffer
  uint32_t instance_metadata_offset; // Offset into instance metadata buffer
  uint32_t flags; // Bitfield: visibility, pass ID, etc.

  // Per-view geometry slice (used by shaders to index correct ranges)
  uint32_t first_index; // Start index within the mesh index buffer (indexed)
  int32_t base_vertex; // Base vertex offset to add to indices / SV_VertexID
  uint32_t padding; // Reserved for alignment
};

// Expected packed size in bytes (12 x uint32_t) as required by shaders.
static_assert(sizeof(DrawMetadata) == 12U * sizeof(uint32_t),
  "Unexpected DrawMetadata size (packing change?)");

// Ensure 16-byte alignment for GPU constant buffer requirements.
static_assert(alignof(DrawMetadata) >= 16,
  "DrawMetadata must be 16-byte aligned for GPU usage");

} // namespace oxygen::engine
