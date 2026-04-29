//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <glm/vec4.hpp>
#include <Oxygen/Vortex/Shadows/Types/PointShadowBinding.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Shadows/Types/SpotShadowBinding.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kShadowTechniqueDirectionalConventional
  = 1U << 0U;
inline constexpr std::uint32_t kShadowTechniqueSpotConventional = 1U << 1U;
inline constexpr std::uint32_t kShadowTechniquePointConventional = 1U << 2U;
inline constexpr std::uint32_t kShadowSamplingContractTexture2DArray
  = 1U << 0U;
inline constexpr std::uint32_t kShadowSamplingContractTextureCubeArray
  = 1U << 1U;

//! Bindless directional conventional-shadow routing payload for one view.
struct alignas(packing::kShaderDataFieldAlignment) ShadowFrameBindings {
  static constexpr std::uint32_t kMaxCascades = 4U;
  static constexpr std::uint32_t kMaxSpotShadows = 8U;
  static constexpr std::uint32_t kMaxPointShadows = 4U;

  ShaderVisibleIndex conventional_shadow_surface_handle {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t cascade_count { 0U };
  std::uint32_t technique_flags { 0U };
  std::uint32_t sampling_contract_flags { 0U };
  glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };

  ShaderVisibleIndex spot_shadow_surface_handle { kInvalidShaderVisibleIndex };
  std::uint32_t spot_shadow_count { 0U };
  std::uint32_t _padding0 { 0U };
  std::uint32_t _padding1 { 0U };

  std::array<ShadowCascadeBinding, kMaxCascades> cascades {};
  std::array<SpotShadowBinding, kMaxSpotShadows> spot_shadows {};
  ShaderVisibleIndex point_shadow_surface_handle { kInvalidShaderVisibleIndex };
  std::uint32_t point_shadow_count { 0U };
  std::uint32_t _padding2 { 0U };
  std::uint32_t _padding3 { 0U };
  std::array<PointShadowBinding, kMaxPointShadows> point_shadows {};

  [[nodiscard]] auto HasDirectionalConventionalShadow() const noexcept -> bool
  {
    return conventional_shadow_surface_handle.IsValid() && cascade_count > 0U
      && (technique_flags & kShadowTechniqueDirectionalConventional) != 0U;
  }

  [[nodiscard]] auto HasSpotConventionalShadow() const noexcept -> bool
  {
    return spot_shadow_surface_handle.IsValid() && spot_shadow_count > 0U
      && (technique_flags & kShadowTechniqueSpotConventional) != 0U;
  }

  [[nodiscard]] auto HasPointConventionalShadow() const noexcept -> bool
  {
    return point_shadow_surface_handle.IsValid() && point_shadow_count > 0U
      && (technique_flags & kShadowTechniquePointConventional) != 0U;
  }
};

static_assert(
  alignof(ShadowFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(ShadowFrameBindings) == 3328U);
static_assert(
  offsetof(ShadowFrameBindings, conventional_shadow_surface_handle) == 0U);
static_assert(offsetof(ShadowFrameBindings, cascade_count) == 4U);
static_assert(offsetof(ShadowFrameBindings, technique_flags) == 8U);
static_assert(offsetof(ShadowFrameBindings, sampling_contract_flags) == 12U);
static_assert(offsetof(ShadowFrameBindings, light_direction_to_source) == 16U);
static_assert(offsetof(ShadowFrameBindings, spot_shadow_surface_handle) == 32U);
static_assert(offsetof(ShadowFrameBindings, spot_shadow_count) == 36U);
static_assert(offsetof(ShadowFrameBindings, cascades) == 48U);
static_assert(offsetof(ShadowFrameBindings, spot_shadows) == 496U);
static_assert(
  offsetof(ShadowFrameBindings, point_shadow_surface_handle) == 1520U);
static_assert(offsetof(ShadowFrameBindings, point_shadow_count) == 1524U);
static_assert(offsetof(ShadowFrameBindings, point_shadows) == 1536U);
static_assert(
  sizeof(ShadowFrameBindings) % packing::kShaderDataFieldAlignment == 0U);

} // namespace oxygen::vortex
