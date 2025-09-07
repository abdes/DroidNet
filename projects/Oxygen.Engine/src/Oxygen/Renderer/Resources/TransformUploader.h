//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

struct TransformBufferInfo {
  std::shared_ptr<graphics::Buffer> buffer;
  uint32_t bindless_index = 0;
  std::unordered_map<uint32_t, uint32_t> handle_to_slot;
};

class TransformUploader {
public:
  OXGN_RNDR_API TransformUploader(std::weak_ptr<Graphics> graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  // Deduplication and handle management
  OXGN_RNDR_NDAPI engine::sceneprep::TransformHandle GetOrAllocate(
    const glm::mat4& transform);
  auto Update(engine::sceneprep::TransformHandle handle,
    const glm::mat4& transform) -> void;
  auto BeginFrame() -> void;
  auto GetUniqueTransformCount() const -> std::size_t;
  auto GetTransform(engine::sceneprep::TransformHandle handle) const
    -> glm::mat4;
  auto GetNormalMatrix(engine::sceneprep::TransformHandle handle) const
    -> glm::mat4;
  auto GetWorldMatricesSpan() const noexcept -> std::span<const glm::mat4>;
  auto GetNormalMatricesSpan() const noexcept -> std::span<const glm::mat4>;
  auto GetDirtyIndices() const noexcept -> const std::vector<std::uint32_t>&;
  auto IsValidHandle(engine::sceneprep::TransformHandle handle) const -> bool;

  // GPU upload API (existing)
  auto ProcessTransforms(const std::vector<glm::mat4>& transforms)
    -> TransformBufferInfo;

private:
  // Deduplication and state
  std::unordered_map<std::uint64_t, engine::sceneprep::TransformHandle>
    transform_key_to_handle_;
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;
  std::vector<std::uint32_t> world_versions_;
  std::vector<std::uint32_t> normal_versions_;
  std::uint32_t global_version_ { 0U };
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  engine::sceneprep::TransformHandle next_handle_ { 0U };

  // Quantized key helper
  static inline auto MakeTransformKey(const glm::mat4& m) noexcept
    -> std::uint64_t
  {
    constexpr float scale = 1024.0f;
    auto q = [&](float v) -> std::int32_t {
      return static_cast<std::int32_t>(std::lround(v * scale));
    };
    std::uint64_t a = static_cast<std::uint32_t>(q(m[0][0]) & 0xFFFF);
    std::uint64_t b = static_cast<std::uint32_t>(q(m[1][1]) & 0xFFFF);
    std::uint64_t c = static_cast<std::uint32_t>(q(m[2][2]) & 0xFFFF);
    std::uint64_t d = static_cast<std::uint32_t>(q(m[3][3]) & 0xFFFF);
    return (a) | (b << 16) | (c << 32) | (d << 48);
  }

  // GPU upload dependencies
  std::weak_ptr<Graphics> graphics_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
};

} // namespace oxygen::renderer::resources
