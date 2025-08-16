//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/TransformManager.h>

#include <algorithm>

namespace oxygen::engine::sceneprep {

auto TransformManager::GetOrAllocate(const glm::mat4& transform)
  -> TransformHandle
{
  const auto key = MakeTransformKey(transform);
  if (const auto it = transform_key_to_handle_.find(key);
    it != transform_key_to_handle_.end()) {
    const auto h = it->second;
    const auto idx = static_cast<std::size_t>(h.get());
    if (idx < transforms_.size() && transforms_[idx] == transform) {
      return h; // exact match
    }
  }

  // Not found or collision mismatch: allocate new handle
  const auto handle = next_handle_;
  const auto idx = static_cast<std::size_t>(handle.get());
  if (transforms_.size() <= idx) {
    transforms_.resize(idx + 1U);
  }
  transforms_[idx] = transform;
  transform_key_to_handle_[key] = handle;
  next_handle_ = TransformHandle { handle.get() + 1U };
  pending_uploads_.push_back(transform);
  return handle;
}

auto TransformManager::FlushPendingUploads() -> void
{
  // Stub: in a real implementation, this would upload to GPU.
  pending_uploads_.clear();
}

auto TransformManager::GetUniqueTransformCount() const -> std::size_t
{
  return transforms_.size();
}

auto TransformManager::GetTransform(TransformHandle handle) const -> glm::mat4
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx < transforms_.size()) {
    return transforms_[idx];
  }
  return glm::mat4(1.0f);
}

auto TransformManager::IsValidHandle(TransformHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < transforms_.size();
}

} // namespace oxygen::engine::sceneprep
