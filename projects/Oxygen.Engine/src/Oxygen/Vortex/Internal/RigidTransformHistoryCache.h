//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/mat4x4.hpp>

#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

class RigidTransformHistoryCache {
public:
  struct Snapshot {
    glm::mat4 current_world { 1.0F };
    glm::mat4 previous_world { 1.0F };
    bool previous_valid { false };
  };

  OXGN_VRTX_API void BeginFrame(std::uint64_t frame_sequence);
  OXGN_VRTX_API auto TouchCurrent(
    scene::NodeHandle node_handle, const glm::mat4& current_world) -> Snapshot;
  OXGN_VRTX_API void EndFrame();

private:
  struct Entry {
    glm::mat4 current_world { 1.0F };
    glm::mat4 previous_world { 1.0F };
    std::uint64_t last_seen_frame { 0U };
    bool previous_valid { false };
  };

  std::uint64_t current_frame_ { 0U };
  std::unordered_map<scene::NodeHandle, Entry> entries_;
};

} // namespace oxygen::vortex::internal
