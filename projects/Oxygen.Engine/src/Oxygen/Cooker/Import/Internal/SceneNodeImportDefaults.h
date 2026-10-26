//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::content::import::detail {

//! The flags of an imported model node, as `NodeRecord` stores them.
struct ImportedNodeFlags {
  uint32_t node_flags = 0U;
  uint32_t inherited_flags = 0U;
};

//! An imported node's flags: visibility, shadow casting and shadow receiving
//! inherit from its parent, so a scene that instances the model governs them
//! from the instance root, down to any node a change sets locally. A node the
//! source hides keeps a local Hidden. Static and ray-casting selectable have
//! no source mode and stay off.
[[nodiscard]] constexpr auto BuildImportedNodeFlags(const bool visible) noexcept
  -> ImportedNodeFlags
{
  using data::pak::world::kSceneNodeFlag_Visible;
  using data::pak::world::kSceneNodeFlags_Inheritable;
  return ImportedNodeFlags {
    .node_flags = 0U,
    .inherited_flags = visible
      ? kSceneNodeFlags_Inheritable
      : kSceneNodeFlags_Inheritable & ~kSceneNodeFlag_Visible,
  };
}

} // namespace oxygen::content::import::detail
