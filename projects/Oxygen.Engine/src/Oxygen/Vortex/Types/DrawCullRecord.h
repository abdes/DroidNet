//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>

namespace oxygen::vortex {

//! What occlusion culling needs to know about one draw.
enum class DrawCullFlagBits : std::uint8_t {
  kOpaque = 1U << 0U,
  kMasked = 1U << 1U,
  kTransparent = 1U << 2U,
  kShadowCaster = 1U << 3U,
  kMainViewVisible = 1U << 4U,
  //! The box is a world-space AABB, not local to the draw's transform.
  kWorldSpaceBox = 1U << 5U,
  //! The draw is never culled (its bounds are not finite).
  kAlwaysVisible = 1U << 6U,
};

[[nodiscard]] constexpr auto ToUnderlying(const DrawCullFlagBits bit) noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(bit);
}

//! Per-draw culling record, one per draw in draw-metadata order.
/*!
 A single draw stores its mesh view's local box; the cull kernels transform it
 by the draw's world matrix into an oriented box. An instanced batch stores the
 world AABB union of its instances and sets `kWorldSpaceBox`. History slots
 are per culling view, so they live in each view's own slot buffer.

 ABI: mirrored by `Contracts/Draw/DrawCullRecord.hlsli`.
*/
struct DrawCullRecord {
  glm::vec3 box_center { 0.0F };
  std::uint32_t _pad0 { 0U };
  glm::vec3 box_extent { 0.0F };
  std::uint32_t flags { 0U };
};

static_assert(sizeof(DrawCullRecord) == 32);
static_assert(offsetof(DrawCullRecord, box_extent) == 16);
static_assert(offsetof(DrawCullRecord, flags) == 28); // NOLINT(*-magic-numbers)

} // namespace oxygen::vortex
