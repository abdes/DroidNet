//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>

namespace oxygen::vortex::occlusion::internal {

auto HistorySlotAllocator::KeyHash::operator()(
  const PreparedSceneFrame::DrawSource& key) const noexcept -> std::size_t
{
  auto seed = std::hash<scene::NodeHandle> {}(key.node);
  HashCombine(seed, key.lod_index);
  HashCombine(seed, key.submesh_index);
  HashCombine(seed, key.mesh_view_index);
  return seed;
}

auto HistorySlotAllocator::KeyEqual::operator()(
  const PreparedSceneFrame::DrawSource& lhs,
  const PreparedSceneFrame::DrawSource& rhs) const noexcept -> bool
{
  return lhs.node == rhs.node && lhs.lod_index == rhs.lod_index
    && lhs.submesh_index == rhs.submesh_index
    && lhs.mesh_view_index == rhs.mesh_view_index;
}

auto HistorySlotAllocator::Update(
  const std::span<const PreparedSceneFrame::DrawSource> draws)
  -> std::vector<Assignment>
{
  auto counts = std::unordered_map<PreparedSceneFrame::DrawSource,
    std::uint32_t, KeyHash, KeyEqual> {};
  counts.reserve(draws.size());
  for (const auto& draw : draws) {
    ++counts[draw];
  }

  // Free the slots of keys that are absent or no longer unique.
  std::erase_if(slots_, [&](const auto& entry) -> bool {
    const auto found = counts.find(entry.first);
    if (found != counts.end() && found->second == 1U) {
      return false;
    }
    free_slots_.push_back(entry.second);
    return true;
  });
  std::ranges::sort(free_slots_, std::greater {});

  auto assignments = std::vector<Assignment>(draws.size());
  for (const auto& [position, draw] : std::views::enumerate(draws)) {
    const auto index = static_cast<std::size_t>(position);
    if (counts.at(draw) != 1U) {
      continue;
    }
    if (const auto kept = slots_.find(draw); kept != slots_.end()) {
      assignments.at(index)
        = Assignment { .slot = kept->second, .fresh = false };
      continue;
    }
    const auto slot = AllocateSlot();
    slots_.emplace(draw, slot);
    assignments.at(index) = Assignment { .slot = slot, .fresh = true };
  }
  return assignments;
}

auto HistorySlotAllocator::Reset() -> void
{
  slots_.clear();
  free_slots_.clear();
  for (std::uint32_t slot = capacity_; slot > 0U; --slot) {
    free_slots_.push_back(slot - 1U);
  }
}

auto HistorySlotAllocator::AllocateSlot() -> std::uint32_t
{
  if (free_slots_.empty()) {
    CHECK_LT_F(capacity_, kFreshHistorySlotBit, "history slots exhausted");
    return capacity_++;
  }
  const auto slot = free_slots_.back();
  free_slots_.pop_back();
  return slot;
}

} // namespace oxygen::vortex::occlusion::internal
