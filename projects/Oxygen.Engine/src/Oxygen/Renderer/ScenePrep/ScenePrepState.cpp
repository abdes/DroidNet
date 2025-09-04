//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>

namespace oxygen::engine::sceneprep {

auto ScenePrepState::ResetFrameData() -> void
{
  // Clear collection phase data
  collected_items.clear();
  filtered_indices.clear();
  pass_masks.clear();

  // Reset per-frame caches while preserving persistent data
  geometry_cache.Reset();
}

} // namespace oxygen::engine::sceneprep
