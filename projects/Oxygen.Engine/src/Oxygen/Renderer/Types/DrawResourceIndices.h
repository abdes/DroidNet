//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::engine {

//! Shader-visible indices for current vertex/index buffers (transitional).
/*!
 Provides the descriptor heap indices for the currently selected
 vertex and index buffers, plus a flag for indexed draws. This is a
 Phase 1–2 migration aid toward fully bindless rendering where per-item
 indices are derived automatically.

 ### Usage Notes

 - Populated on the CPU and uploaded to a structured buffer SRV.
 - Shaders read entry 0 via a dynamic bindless slot provided in
   SceneConstants (see bindless_indices_slot).
 - The slot value may change per frame; do not assume a fixed slot.

 @see SceneConstants
*/
struct DrawResourceIndices {
  std::uint32_t vertex_buffer_index;
  std::uint32_t index_buffer_index;
  std::uint32_t is_indexed; // 1 if indexed draw, 0 otherwise
};

// Expected packed size in bytes (3 x uint32_t) as required by shaders.
static_assert(sizeof(DrawResourceIndices) == 3U * sizeof(std::uint32_t),
  "Unexpected DrawResourceIndices size (packing change?)");

} // namespace oxygen::engine
