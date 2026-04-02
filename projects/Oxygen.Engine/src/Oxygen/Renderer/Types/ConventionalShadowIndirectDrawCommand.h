//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oxygen::renderer {

//! Packed indirect draw command for counted conventional shadow raster.
/*!
 Matches `ExecuteIndirect(kDrawWithRootConstant)`: DWORD0 carries the global
 `draw_index`, followed by the native draw-arguments payload.
*/
struct ConventionalShadowIndirectDrawCommand {
  std::uint32_t draw_index { 0U };
  std::uint32_t vertex_count_per_instance { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_vertex_location { 0U };
  std::uint32_t start_instance_location { 0U };

  auto operator==(const ConventionalShadowIndirectDrawCommand&) const -> bool
    = default;
};

static_assert(std::is_standard_layout_v<ConventionalShadowIndirectDrawCommand>);
static_assert(sizeof(ConventionalShadowIndirectDrawCommand) == 20U);
static_assert(
  offsetof(ConventionalShadowIndirectDrawCommand, draw_index) == 0U);
static_assert(
  offsetof(ConventionalShadowIndirectDrawCommand, vertex_count_per_instance)
  == 4U);
static_assert(
  offsetof(ConventionalShadowIndirectDrawCommand, instance_count) == 8U);
static_assert(
  offsetof(ConventionalShadowIndirectDrawCommand, start_vertex_location)
  == 12U);
static_assert(
  offsetof(ConventionalShadowIndirectDrawCommand, start_instance_location)
  == 16U);

} // namespace oxygen::renderer
