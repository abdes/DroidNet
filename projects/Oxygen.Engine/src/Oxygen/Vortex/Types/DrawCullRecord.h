//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/vec3.hpp>

namespace oxygen::vortex {

//! What occlusion culling needs to know about one draw.
enum class DrawCullFlagBits : std::uint32_t {
  kOpaque = 1U << 0U,
  kMasked = 1U << 1U,
  kTransparent = 1U << 2U,
  kShadowCaster = 1U << 3U,
  kMainViewVisible = 1U << 4U,
  //! The box is a world-space AABB, not local to the draw's transform.
  kWorldSpaceBox = 1U << 5U,
  //! The draw is never culled (its bounds are not finite).
  kAlwaysVisible = 1U << 6U,
  //! The draw's history slot is new this frame and reads as not visible.
  kFreshHistory = 1U << 7U,
};

[[nodiscard]] constexpr auto ToUnderlying(const DrawCullFlagBits bit) noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(bit);
}

//! The draw has no visibility history slot.
inline constexpr std::uint32_t kNoHistorySlot
  = (std::numeric_limits<std::uint32_t>::max)();

//! Per-draw culling record, one per draw in draw-metadata order.
/*!
 A single draw stores its mesh view's local box; the cull kernels transform it
 by the draw's world matrix into an oriented box. An instanced batch stores the
 world AABB union of its instances and sets `kWorldSpaceBox`.

 ABI: mirrored by `Contracts/Draw/DrawCullRecord.hlsli`.
*/
struct DrawCullRecord {
  glm::vec3 box_center { 0.0F };
  std::uint32_t history_slot { kNoHistorySlot };
  glm::vec3 box_extent { 0.0F };
  std::uint32_t flags { 0U };
};

static_assert(sizeof(DrawCullRecord) == 32);
static_assert(offsetof(DrawCullRecord, history_slot) == 12);
static_assert(offsetof(DrawCullRecord, box_extent) == 16);
static_assert(offsetof(DrawCullRecord, flags) == 28);

} // namespace oxygen::vortex
