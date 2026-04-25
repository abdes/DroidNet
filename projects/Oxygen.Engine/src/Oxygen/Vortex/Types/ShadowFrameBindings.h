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
#include <glm/vec4.hpp>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kShadowTechniqueDirectionalConventional
  = 1U << 0U;
inline constexpr std::uint32_t kShadowSamplingContractTexture2DArray
  = 1U << 0U;

//! Bindless directional conventional-shadow routing payload for one view.
struct alignas(packing::kShaderDataFieldAlignment) ShadowFrameBindings {
  static constexpr std::uint32_t kMaxCascades = 4U;

  ShaderVisibleIndex conventional_shadow_surface_handle {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t cascade_count { 0U };
  std::uint32_t technique_flags { 0U };
  std::uint32_t sampling_contract_flags { 0U };
  glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };

  std::array<ShadowCascadeBinding, kMaxCascades> cascades {};

  [[nodiscard]] auto HasDirectionalConventionalShadow() const noexcept -> bool
  {
    return conventional_shadow_surface_handle.IsValid() && cascade_count > 0U
      && (technique_flags & kShadowTechniqueDirectionalConventional) != 0U;
  }
};

static_assert(
  alignof(ShadowFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(ShadowFrameBindings) % packing::kShaderDataFieldAlignment == 0U);

} // namespace oxygen::vortex
