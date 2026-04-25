//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

//! Narrow shader-facing shadow-caster record for conventional shadow culling.
/*!
 Derived from finalized per-draw metadata so the conventional shadow path can
 build one stable draw-record stream per view, then reuse it across all shadow
 jobs without replaying scene-prep work.
*/
struct alignas(16) ConventionalShadowDrawRecord {
  glm::vec4 world_bounding_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
  std::uint32_t draw_index { 0U };
  std::uint32_t partition_index { 0U };
  PassMask partition_pass_mask {};
  std::uint32_t primitive_flags { 0U };

  auto operator==(const ConventionalShadowDrawRecord&) const -> bool = default;
};

static_assert(std::is_standard_layout_v<ConventionalShadowDrawRecord>);
static_assert(sizeof(ConventionalShadowDrawRecord) == 32U);
static_assert(
  offsetof(ConventionalShadowDrawRecord, world_bounding_sphere) == 0U);
static_assert(offsetof(ConventionalShadowDrawRecord, draw_index) == 16U);
static_assert(offsetof(ConventionalShadowDrawRecord, partition_index) == 20U);
static_assert(
  offsetof(ConventionalShadowDrawRecord, partition_pass_mask) == 24U);
static_assert(offsetof(ConventionalShadowDrawRecord, primitive_flags) == 28U);

[[nodiscard]] constexpr auto IsStaticShadowCaster(
  const ConventionalShadowDrawRecord& record) noexcept -> bool
{
  return HasAnyDrawPrimitiveFlag(
    record.primitive_flags, DrawPrimitiveFlagBits::kStaticShadowCaster);
}

[[nodiscard]] constexpr auto IsMainViewVisible(
  const ConventionalShadowDrawRecord& record) noexcept -> bool
{
  return HasAnyDrawPrimitiveFlag(
    record.primitive_flags, DrawPrimitiveFlagBits::kMainViewVisible);
}

} // namespace oxygen::vortex
