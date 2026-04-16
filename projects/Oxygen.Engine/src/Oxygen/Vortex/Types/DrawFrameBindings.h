//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

// Strong slot wrappers for draw-system shader-visible bindings.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(TypeName)                        \
  struct TypeName {                                                            \
    ShaderVisibleIndex value;                                                  \
    TypeName()                                                                 \
      : TypeName(kInvalidShaderVisibleIndex)                                   \
    {                                                                          \
    }                                                                          \
    explicit constexpr TypeName(const ShaderVisibleIndex v)                    \
      : value(v)                                                               \
    {                                                                          \
    }                                                                          \
    [[nodiscard]] constexpr auto IsValid() const noexcept                      \
    {                                                                          \
      return value != kInvalidShaderVisibleIndex;                              \
    }                                                                          \
    constexpr auto operator<=>(const TypeName&) const = default;               \
  };                                                                           \
  static_assert(sizeof(TypeName) == 4)

OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessDrawMetadataSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessWorldsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessNormalsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessMaterialShadingConstantsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(
  BindlessProceduralGridMaterialConstantsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessInstanceDataSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessSkinnedPosePublicationsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessMorphPublicationsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessMaterialWpoPublicationsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(
  BindlessMotionVectorStatusPublicationsSlot);
OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE(BindlessVelocityDrawMetadataSlot);

#undef OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE

//! Bindless draw-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) DrawFrameBindings {
  BindlessDrawMetadataSlot draw_metadata_slot {};
  BindlessWorldsSlot current_worlds_slot {};
  BindlessWorldsSlot previous_worlds_slot {};
  BindlessNormalsSlot normal_matrices_slot {};
  BindlessMaterialShadingConstantsSlot material_shading_constants_slot {};
  BindlessProceduralGridMaterialConstantsSlot
    procedural_grid_material_constants_slot {};
  BindlessInstanceDataSlot instance_data_slot {};
  BindlessSkinnedPosePublicationsSlot current_skinned_pose_slot {};
  BindlessSkinnedPosePublicationsSlot previous_skinned_pose_slot {};
  BindlessMorphPublicationsSlot current_morph_slot {};
  BindlessMorphPublicationsSlot previous_morph_slot {};
  BindlessMaterialWpoPublicationsSlot current_material_wpo_slot {};
  BindlessMaterialWpoPublicationsSlot previous_material_wpo_slot {};
  BindlessMotionVectorStatusPublicationsSlot
    current_motion_vector_status_slot {};
  BindlessMotionVectorStatusPublicationsSlot
    previous_motion_vector_status_slot {};
  BindlessVelocityDrawMetadataSlot velocity_draw_metadata_slot {};
};

static_assert(sizeof(DrawFrameBindings) == 64);
static_assert(alignof(DrawFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(DrawFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
