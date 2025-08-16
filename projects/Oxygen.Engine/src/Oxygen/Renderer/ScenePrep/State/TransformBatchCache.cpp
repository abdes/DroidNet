//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/TransformBatchCache.h>

namespace oxygen::engine::sceneprep {

auto TransformBatchCache::MapItemToHandle(
  std::size_t item_idx, TransformHandle handle) -> void
{
  if (item_idx >= item_to_handle_.size()) {
    item_to_handle_.resize(item_idx + 1U, TransformHandle { 0U });
  }
  if (item_to_handle_[item_idx].get() == 0U) {
    ++mapped_count_;
  }
  item_to_handle_[item_idx] = handle;
}

auto TransformBatchCache::GetHandle(std::size_t item_idx) const
  -> std::optional<TransformHandle>
{
  if (item_idx >= item_to_handle_.size()) {
    return std::nullopt;
  }
  const auto h = item_to_handle_[item_idx];
  if (h.get() == 0U) {
    return std::nullopt;
  }
  return h;
}

auto TransformBatchCache::Reset() -> void
{
  item_to_handle_.clear();
  mapped_count_ = 0U;
}

auto TransformBatchCache::IsEmpty() const -> bool
{
  return mapped_count_ == 0U;
}

auto TransformBatchCache::GetMappedItemCount() const -> std::size_t
{
  return mapped_count_;
}

} // namespace oxygen::engine::sceneprep
