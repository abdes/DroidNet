//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Internal/DeformationHistoryCache.h>

namespace oxygen::vortex::internal {

void DeformationHistoryCache::BeginFrame(const std::uint64_t frame_sequence,
  const observer_ptr<const scene::Scene> scene)
{
  current_frame_ = frame_sequence;
  if (current_scene_ != scene.get()) {
    material_wpo_entries_.clear();
    motion_vector_status_entries_.clear();
    current_scene_ = scene.get();
  }
}

auto DeformationHistoryCache::TouchCurrentMaterialWpo(
  const RenderMotionIdentityKey& key, const MaterialWpoPublication& current)
  -> PublicationHistorySnapshot<MaterialWpoPublication>
{
  return TouchCurrent(material_wpo_entries_, key, current);
}

auto DeformationHistoryCache::TouchCurrentMotionVectorStatus(
  const RenderMotionIdentityKey& key,
  const MotionVectorStatusPublication& current)
  -> PublicationHistorySnapshot<MotionVectorStatusPublication>
{
  return TouchCurrent(motion_vector_status_entries_, key, current);
}

void DeformationHistoryCache::EndFrame()
{
  EndFrame(material_wpo_entries_);
  EndFrame(motion_vector_status_entries_);
}

template <typename PublicationT>
auto DeformationHistoryCache::TouchCurrent(
  std::unordered_map<RenderMotionIdentityKey, Entry<PublicationT>,
    RenderMotionIdentityKeyHash>& entries,
  const RenderMotionIdentityKey& key, const PublicationT& current)
  -> PublicationHistorySnapshot<PublicationT>
{
  auto& entry = entries[key];
  entry.last_seen_frame = current_frame_;

  const auto snapshot = PublicationHistorySnapshot<PublicationT> {
    .current = current,
    .previous = entry.previous_valid ? entry.previous : current,
    .previous_valid = entry.previous_valid,
  };

  entry.current = current;
  if (!entry.previous_valid) {
    entry.previous = current;
    entry.previous_valid = true;
  }
  return snapshot;
}

template <typename PublicationT>
void DeformationHistoryCache::EndFrame(
  std::unordered_map<RenderMotionIdentityKey, Entry<PublicationT>,
    RenderMotionIdentityKeyHash>& entries)
{
  for (auto it = entries.begin(); it != entries.end();) {
    auto& entry = it->second;
    if (entry.last_seen_frame != current_frame_) {
      it = entries.erase(it);
      continue;
    }
    entry.previous = entry.current;
    entry.previous_valid = true;
    ++it;
  }
}

template auto DeformationHistoryCache::TouchCurrent(
  std::unordered_map<RenderMotionIdentityKey, Entry<MaterialWpoPublication>,
    RenderMotionIdentityKeyHash>&,
  const RenderMotionIdentityKey&, const MaterialWpoPublication&)
  -> PublicationHistorySnapshot<MaterialWpoPublication>;

template auto DeformationHistoryCache::TouchCurrent(
  std::unordered_map<RenderMotionIdentityKey,
    Entry<MotionVectorStatusPublication>, RenderMotionIdentityKeyHash>&,
  const RenderMotionIdentityKey&, const MotionVectorStatusPublication&)
  -> PublicationHistorySnapshot<MotionVectorStatusPublication>;

template void DeformationHistoryCache::EndFrame(
  std::unordered_map<RenderMotionIdentityKey, Entry<MaterialWpoPublication>,
    RenderMotionIdentityKeyHash>&);

template void DeformationHistoryCache::EndFrame(
  std::unordered_map<RenderMotionIdentityKey,
    Entry<MotionVectorStatusPublication>, RenderMotionIdentityKeyHash>&);

} // namespace oxygen::vortex::internal
