//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/MaterialUploadCache.h>

#include <algorithm>

namespace oxygen::engine::sceneprep {

auto MaterialUploadCache::RecordMaterialIndex(
  std::size_t item_idx, MaterialHandle handle) -> void
{
  if (item_idx >= item_to_material_.size()) {
    item_to_material_.resize(item_idx + 1U, MaterialHandle { 0U });
  }
  if (item_to_material_[item_idx].get() == 0U) {
    ++cached_count_;
  }
  item_to_material_[item_idx] = handle;
}

auto MaterialUploadCache::GetMaterialHandle(std::size_t item_idx) const
  -> std::optional<MaterialHandle>
{
  if (item_idx >= item_to_material_.size()) {
    return std::nullopt;
  }
  const auto h = item_to_material_[item_idx];
  if (h.get() == 0U) {
    return std::nullopt;
  }
  return h;
}

auto MaterialUploadCache::Reset() -> void
{
  item_to_material_.clear();
  cached_count_ = 0U;
}

auto MaterialUploadCache::IsEmpty() const -> bool
{
  return cached_count_ == 0U;
}

auto MaterialUploadCache::GetCachedMaterialCount() const -> std::size_t
{
  return cached_count_;
}

} // namespace oxygen::engine::sceneprep
