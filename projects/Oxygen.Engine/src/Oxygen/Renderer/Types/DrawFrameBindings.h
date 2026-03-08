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

namespace oxygen::engine {

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

#undef OXYGEN_DEFINE_DRAW_BINDLESS_SLOT_TYPE

//! Bindless draw-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) DrawFrameBindings {
  BindlessDrawMetadataSlot draw_metadata_slot {};
  BindlessWorldsSlot transforms_slot {};
  BindlessNormalsSlot normal_matrices_slot {};
  BindlessMaterialShadingConstantsSlot material_shading_constants_slot {};
  BindlessProceduralGridMaterialConstantsSlot
    procedural_grid_material_constants_slot {};
  BindlessInstanceDataSlot instance_data_slot {};
  std::array<std::uint32_t, 2> _pad_to_16 {};
};

static_assert(sizeof(DrawFrameBindings) == 32);
static_assert(alignof(DrawFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(DrawFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::engine
