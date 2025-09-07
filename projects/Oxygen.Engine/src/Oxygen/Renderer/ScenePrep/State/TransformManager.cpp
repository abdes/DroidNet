#if 0
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#  include <Oxygen/Renderer/ScenePrep/State/TransformManager.h>

#  include <algorithm>

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
    normal_matrices_.resize(idx + 1U);
    world_versions_.resize(idx + 1U);
    normal_versions_.resize(idx + 1U);
    dirty_epoch_.resize(idx + 1U);
  }
  transforms_[idx] = transform;
  // Compute normal matrix (inverse transpose upper-left 3x3) lazily here.
  const glm::mat3 upper(transform);
  // Fast path: if matrix is orthonormal (approx), normal = upper directly.
  // Simple heuristic: columns length ~1 and determinant ~1.
  const float det = glm::determinant(upper);
  glm::mat3 normal3;
  if (std::abs(det - 1.0f) < 1e-3f) {
    normal3 = upper;
  } else {
    normal3 = glm::transpose(glm::inverse(upper));
  }
  glm::mat4 normal4(1.0f);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      normal4[c][r] = normal3[r][c];
    }
  }
  normal_matrices_[idx] = normal4;
  world_versions_[idx] = global_version_;
  normal_versions_[idx] = global_version_;
  // Mark dirty for this frame.
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
  transform_key_to_handle_[key] = handle;
  next_handle_ = TransformHandle { handle.get() + 1U };
  pending_uploads_.push_back(transform);
  return handle;
}

auto TransformManager::Update(
  TransformHandle handle, const glm::mat4& transform) -> void
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx >= transforms_.size()) {
    return; // invalid
  }
  if (transforms_[idx] == transform) {
    return; // no change
  }
  ++global_version_;
  transforms_[idx] = transform;
  const glm::mat3 upper(transform);
  const float det = glm::determinant(upper);
  glm::mat3 normal3;
  if (std::abs(det - 1.0f) < 1e-3f) {
    normal3 = upper;
  } else {
    normal3 = glm::transpose(glm::inverse(upper));
  }
  glm::mat4 normal4(1.0f);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      normal4[c][r] = normal3[r][c];
    }
  }
  normal_matrices_[idx] = normal4;
  world_versions_[idx] = global_version_;
  normal_versions_[idx] = global_version_;
  pending_uploads_.push_back(transform);
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
}

auto TransformManager::BeginFrame() -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) { // wrapped
    current_epoch_ = 1U;
    std::fill(dirty_epoch_.begin(), dirty_epoch_.end(), 0U);
  }
  dirty_indices_.clear();
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

auto TransformManager::GetNormalMatrix(TransformHandle handle) const
  -> glm::mat4
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx < normal_matrices_.size()) {
    return normal_matrices_[idx];
  }
  return glm::mat4(1.0f);
}

auto TransformManager::IsValidHandle(TransformHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < transforms_.size();
}

} // namespace oxygen::engine::sceneprep
#endif
