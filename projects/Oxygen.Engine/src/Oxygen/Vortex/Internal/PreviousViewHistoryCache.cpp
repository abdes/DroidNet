//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/epsilon.hpp>

#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>

namespace {

auto Mat4Equal(const glm::mat4& a, const glm::mat4& b, const float epsilon)
  -> bool
{
  for (int i = 0; i < 4; ++i) {
    if (!glm::all(glm::epsilonEqual(a[i], b[i], epsilon))) {
      return false;
    }
  }
  return true;
}

auto ViewPortMatches(
  const oxygen::ViewPort& lhs, const oxygen::ViewPort& rhs) noexcept -> bool
{
  constexpr float epsilon = 1.0e-5F;
  return glm::all(glm::epsilonEqual(
           glm::vec2 { lhs.top_left_x, lhs.top_left_y },
           glm::vec2 { rhs.top_left_x, rhs.top_left_y }, epsilon))
    && glm::all(glm::epsilonEqual(
      glm::vec2 { lhs.width, lhs.height }, glm::vec2 { rhs.width, rhs.height },
      epsilon))
    && glm::all(glm::epsilonEqual(
      glm::vec2 { lhs.min_depth, lhs.max_depth },
      glm::vec2 { rhs.min_depth, rhs.max_depth }, epsilon));
}

} // namespace

namespace oxygen::vortex::internal {

void PreviousViewHistoryCache::BeginFrame(const std::uint64_t frame_sequence,
  const observer_ptr<const scene::Scene> scene)
{
  current_frame_ = frame_sequence;
  if (current_scene_ != scene.get()) {
    entries_.clear();
    current_scene_ = scene.get();
  }
}

auto PreviousViewHistoryCache::TouchCurrent(
  const ViewId view_id, const CurrentState& current) -> Snapshot
{
  auto& entry = entries_[view_id];
  entry.last_seen_frame = current_frame_;

  constexpr float epsilon = 1.0e-5F;
  const bool invalidated = !entry.previous_valid
    || !ViewPortMatches(entry.current.viewport, current.viewport)
    || !Mat4Equal(
      entry.current.stable_projection_matrix, current.stable_projection_matrix,
      epsilon);

  const auto snapshot = Snapshot {
    .current = current,
    .previous = invalidated ? current : entry.previous,
    .previous_valid = !invalidated && entry.previous_valid,
  };

  entry.current = current;
  if (invalidated) {
    entry.previous = current;
    entry.previous_valid = false;
  }
  return snapshot;
}

void PreviousViewHistoryCache::EndFrame()
{
  for (auto it = entries_.begin(); it != entries_.end();) {
    auto& entry = it->second;
    if (entry.last_seen_frame != current_frame_) {
      it = entries_.erase(it);
      continue;
    }
    entry.previous = entry.current;
    entry.previous_valid = true;
    ++it;
  }
}

} // namespace oxygen::vortex::internal
