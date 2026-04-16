//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/Types/VelocityPublications.h>

namespace oxygen::vortex {

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
  std::span<const float> previous_world_matrices; // 16 * float per matrix
  std::span<const float> normal_matrices; // 16 * float per matrix

  // Partition map (Task 11 scaffolding): pass mask -> contiguous draw range.
  struct PartitionRange {
    PassMask pass_mask; // bitfield identifying pass categories
    uint32_t begin = 0; // inclusive begin draw index
    uint32_t end = 0; // exclusive end draw index
  };
  std::span<const PartitionRange> partitions; // published ranges (may be empty)
  std::span<const glm::vec4>
    draw_bounding_spheres; // one per draw metadata record
  std::span<const sceneprep::RenderItemData>
    render_items; // per-view collected items captured at scene-prep finalize
  std::span<const glm::vec4>
    shadow_caster_bounding_spheres; // xyz=center, w=radius
  std::span<const glm::vec4>
    visible_receiver_bounding_spheres; // xyz=center, w=radius
  std::span<const vortex::ConventionalShadowDrawRecord>
    conventional_shadow_draw_records;
  std::span<const SkinnedPosePublication> current_skinned_pose_publications;
  std::span<const SkinnedPosePublication> previous_skinned_pose_publications;
  std::span<const MorphPublication> current_morph_publications;
  std::span<const MorphPublication> previous_morph_publications;
  std::span<const MaterialWpoPublication> current_material_wpo_publications;
  std::span<const MaterialWpoPublication> previous_material_wpo_publications;
  std::span<const MotionVectorStatusPublication>
    current_motion_vector_status_publications;
  std::span<const MotionVectorStatusPublication>
    previous_motion_vector_status_publications;
  std::span<const VelocityDrawMetadata> velocity_draw_metadata;

  // Bindless SRV indices captured at ScenePrep finalization time
  // These must be captured immediately after Finalize to ensure consistency
  oxygen::ShaderVisibleIndex bindless_worlds_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_previous_worlds_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_normals_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_material_shading_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_draw_metadata_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_draw_bounds_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_instance_data_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_conventional_shadow_draw_records_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_current_skinned_pose_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_previous_skinned_pose_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_current_morph_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_previous_morph_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_current_material_wpo_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_previous_material_wpo_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_current_motion_vector_status_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_previous_motion_vector_status_slot {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex bindless_velocity_draw_metadata_slot {
    oxygen::kInvalidShaderVisibleIndex
  };

  // View exposure resolved during scene prep (manual or auto baseline).
  float exposure { 1.0F };

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    // For now validity is trivial; will evolve as fields are populated.
    return true;
  }

  [[nodiscard]] auto GetDrawMetadata() const noexcept
    -> std::span<const DrawMetadata>
  {
    if (draw_metadata_bytes.empty()) {
      return {};
    }

    const auto count = draw_metadata_bytes.size() / sizeof(DrawMetadata);
    const auto* metadata
      // NOLINTNEXTLINE(*-reinterpret-cast)
      = reinterpret_cast<const DrawMetadata*>(draw_metadata_bytes.data());
    return { metadata, count };
  }
};

} // namespace oxygen::vortex
