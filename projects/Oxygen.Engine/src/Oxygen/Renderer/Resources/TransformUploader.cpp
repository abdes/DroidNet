//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

TransformUploader::TransformUploader(std::weak_ptr<Graphics> graphics,
  observer_ptr<engine::upload::UploadCoordinator> uploader)
  : graphics_(std::move(graphics))
  , uploader_(uploader)
{
}

TransformUploader::~TransformUploader() = default;

auto TransformUploader::GetOrAllocate(const glm::mat4& transform)
  -> engine::sceneprep::TransformHandle
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
  next_handle_ = engine::sceneprep::TransformHandle { handle.get() + 1U };

  return handle;
}

auto TransformUploader::Update(
  engine::sceneprep::TransformHandle handle, const glm::mat4& transform) -> void
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
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
}

auto TransformUploader::BeginFrame() -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) { // wrapped
    current_epoch_ = 1U;
    std::ranges::fill(dirty_epoch_, 0U);
  }
  dirty_indices_.clear();
}

auto TransformUploader::GetUniqueTransformCount() const -> std::size_t
{
  return transforms_.size();
}

auto TransformUploader::GetTransform(
  engine::sceneprep::TransformHandle handle) const -> glm::mat4
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx < transforms_.size()) {
    return transforms_[idx];
  }
  return { 1.0f };
}

auto TransformUploader::GetNormalMatrix(
  engine::sceneprep::TransformHandle handle) const -> glm::mat4
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx < normal_matrices_.size()) {
    return normal_matrices_[idx];
  }
  return { 1.0f };
}

auto TransformUploader::GetWorldMatricesSpan() const noexcept
  -> std::span<const glm::mat4>
{
  return { transforms_.data(), transforms_.size() };
}

auto TransformUploader::GetNormalMatricesSpan() const noexcept
  -> std::span<const glm::mat4>
{
  return { normal_matrices_.data(), normal_matrices_.size() };
}

auto TransformUploader::GetDirtyIndices() const noexcept
  -> const std::vector<std::uint32_t>&

{
  return dirty_indices_;
}

auto TransformUploader::IsValidHandle(
  engine::sceneprep::TransformHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < transforms_.size();
}

auto TransformUploader::ProcessTransforms(
  const std::vector<glm::mat4>& transforms) -> TransformBufferInfo
{
  // Optionally, this could use the deduplication logic above, but for now, just
  // upload the provided transforms.
  TransformBufferInfo info;
  std::vector<glm::mat4> unique_transforms = transforms;
  for (size_t i = 0; i < transforms.size(); ++i) {
    info.handle_to_slot[static_cast<uint32_t>(i)] = static_cast<uint32_t>(i);
  }
  auto graphics = graphics_.lock();
  if (!graphics) {
    return info;
  }
  auto& allocator = graphics->GetDescriptorAllocator();
  auto& registry = graphics->GetResourceRegistry();
  const uint64_t buffer_size = unique_transforms.size() * sizeof(glm::mat4);
  graphics::BufferDesc desc;
  desc.size_bytes = buffer_size;
  desc.usage = graphics::BufferUsage::kStorage;
  desc.memory = graphics::BufferMemory::kDeviceLocal;
  desc.debug_name = "TransformBuffer";
  std::shared_ptr<graphics::Buffer> buffer = graphics->CreateBuffer(desc);
  engine::upload::UploadRequest req;
  req.kind = engine::upload::UploadKind::kBuffer;
  req.desc = engine::upload::UploadBufferDesc {
    .dst = buffer,
    .size_bytes = buffer_size,
    .dst_offset = 0,
  };
  req.data = engine::upload::UploadDataView { std::as_bytes(
    std::span<const glm::mat4>(unique_transforms)) };
  uploader_->Submit(req);
  registry.Register(buffer);
  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.range = { 0, buffer_size };
  view_desc.stride = sizeof(glm::mat4);
  auto handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  info.buffer = buffer;
  info.bindless_index = allocator.GetShaderVisibleIndex(handle).get();
  registry.RegisterView(*buffer, std::move(handle), view_desc);
  return info;
}

} // namespace oxygen::renderer::resources
