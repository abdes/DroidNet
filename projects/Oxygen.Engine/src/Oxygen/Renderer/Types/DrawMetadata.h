//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Renderer/Types/PassMask.h>

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
struct DrawMetadata {
  // --- Geometry buffers ---
  ShaderVisibleIndex
    vertex_buffer_index; // Bindless index into vertex buffer table
  ShaderVisibleIndex
    index_buffer_index; // Bindless index into index buffer table
  uint32_t first_index; // Start index within the mesh index buffer
  int32_t base_vertex; // Base vertex offset (can be negative)

  // --- Draw configuration ---
  uint32_t is_indexed; // 0 = non-indexed, 1 = indexed
  uint32_t instance_count; // Number of instances (>=1)
  uint32_t index_count; // Number of indices for indexed draws (undefined for
                        // non-indexed)
  uint32_t vertex_count; // Number of vertices for non-indexed draws (undefined
                         // for indexed)
  uint32_t material_handle; // Stable MaterialRegistry handle (0 sentinel)
                            // Formerly material_index (breaking rename)

  // --- Transform & instance indirection ---
  uint32_t transform_index; // Index into world/normal transform arrays
  uint32_t instance_metadata_buffer_index; // Bindless index into instance
                                           // metadata buffer
  uint32_t instance_metadata_offset; // Offset into instance metadata buffer
  PassMask flags; // uint32_t, Bitfield: visibility, pass mask, etc.
};

// Logical field bytes: 13 x 4 = 52. We intentionally use tight packing to
// keep StructuredBuffer stride small; HLSL struct must mirror EXACT order.
static_assert(sizeof(DrawMetadata) == 52,
  "Unexpected DrawMetadata size (expected 52); update HLSL DrawMetadata layout "
  "accordingly");

} // namespace oxygen::engine
