//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Internal/RigidTransformHistoryCache.h>

namespace oxygen::vortex::internal {

void RigidTransformHistoryCache::BeginFrame(const std::uint64_t frame_sequence)
{
  current_frame_ = frame_sequence;
}

auto RigidTransformHistoryCache::TouchCurrent(
  const scene::NodeHandle node_handle, const glm::mat4& current_world)
  -> Snapshot
{
  auto& entry = entries_[node_handle];
  entry.last_seen_frame = current_frame_;

  const auto snapshot = Snapshot {
    .current_world = current_world,
    .previous_world = entry.previous_valid ? entry.previous_world : current_world,
    .previous_valid = entry.previous_valid,
  };

  entry.current_world = current_world;
  if (!entry.previous_valid) {
    entry.previous_world = current_world;
    entry.previous_valid = true;
  }
  return snapshot;
}

void RigidTransformHistoryCache::EndFrame()
{
  for (auto it = entries_.begin(); it != entries_.end();) {
    auto& entry = it->second;
    if (entry.last_seen_frame != current_frame_) {
      it = entries_.erase(it);
      continue;
    }
    entry.previous_world = entry.current_world;
    entry.previous_valid = true;
    ++it;
  }
}

} // namespace oxygen::vortex::internal
