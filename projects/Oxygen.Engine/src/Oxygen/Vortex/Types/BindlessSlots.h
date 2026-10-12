//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

// Strong wrappers for the bindless slots of the view and draw routing records
// (ViewConstants and DrawFrameBindings).
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(TypeName)                             \
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

OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessViewFrameBindingsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessDrawMetadataSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessWorldsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessNormalsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessMaterialShadingConstantsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessProceduralGridMaterialConstantsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessInstanceDataSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessMaterialWpoPublicationsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessMotionVectorStatusPublicationsSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessVelocityDrawMetadataSlot);
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessDrawCullRecordsSlot);

#undef OXYGEN_DEFINE_BINDLESS_SLOT_TYPE

} // namespace oxygen::vortex
