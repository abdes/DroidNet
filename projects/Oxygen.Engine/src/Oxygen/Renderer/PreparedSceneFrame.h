//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Renderer/Types/PassMask.h>

namespace oxygen::engine {

//=== Prepared Scene Frame ===----------------------------------------------//

//! Immutable, per-frame finalized SoA snapshot exposed to render passes.
/*!
 This is a lightweight view (spans) over renderer-owned arrays produced by
 the finalization stage. It intentionally does not own memory so that frame
 lifetime management remains centralized in the renderer.

 Initial minimum surface area (will expand in later tasks):
  - draw_metadata : Per-draw packed metadata (GPU-facing layout) (TBD Task 2)
  - world_transforms / normal_transforms: Matrices indexed by draw
  - partition map (pass -> range) to be added in Task 11

 Construction: Created each frame after finalization, then referenced by
 RenderContext for pass consumption. All spans must remain valid until the
 end of frame execution.

 @note This initial version is intentionally skeletal (Task 1). Additional
       fields and helper accessors will be added in subsequent tasks.
 */
struct PreparedSceneFrame {
  // Spans over finalized arrays (empty initially until finalization wired)
  // Non-owning view of draw metadata bytes. These spans point into renderer
  // owned backing storage (per-view) which ensures stability for the lifetime
  // of the prepared frame.
  std::span<const std::byte> draw_metadata_bytes; // temporary generic span
  std::span<const float> world_matrices; // 16 * float per matrix
  std::span<const float> normal_matrices; // 16 * float per matrix

  // Partition map (Task 11 scaffolding): pass mask -> contiguous draw range.
  struct PartitionRange {
    PassMask pass_mask {}; // bitfield identifying pass categories
    uint32_t begin = 0; // inclusive begin draw index
    uint32_t end = 0; // exclusive end draw index
  };
  std::span<const PartitionRange> partitions; // published ranges (may be empty)

  // Bindless SRV indices captured at ScenePrep finalization time
  // These must be captured immediately after Finalize to ensure consistency
  uint32_t bindless_worlds_slot = 0xFFFFFFFF;
  uint32_t bindless_normals_slot = 0xFFFFFFFF;
  uint32_t bindless_materials_slot = 0xFFFFFFFF;
  uint32_t bindless_draw_metadata_slot = 0xFFFFFFFF;

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    // For now validity is trivial; will evolve as fields are populated.
    return true;
  }
};

} // namespace oxygen::engine
