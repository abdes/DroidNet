//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::content::import::detail {

[[nodiscard]] constexpr auto BuildImportedNodeFlags(const bool visible,
  const bool has_renderable, const bool has_light) noexcept -> std::uint32_t
{
  auto flags = visible ? data::pak::world::kSceneNodeFlag_Visible : 0U;

  // Imported renderable nodes should behave like authored scene nodes:
  // opaque/standard mesh nodes cast and receive shadows by default unless
  // explicitly overridden later in authored scene data.
  if (has_renderable) {
    flags |= data::pak::world::kSceneNodeFlag_CastsShadows
      | data::pak::world::kSceneNodeFlag_ReceivesShadows;
  }

  // Light shadowing is additionally gated by the owning node's shadow flag, so
  // imported light nodes default that capability on. Receiving shadows is not a
  // meaningful node-level concept for light-only helpers.
  if (has_light) {
    flags |= data::pak::world::kSceneNodeFlag_CastsShadows;
  }

  return flags;
}

} // namespace oxygen::content::import::detail
