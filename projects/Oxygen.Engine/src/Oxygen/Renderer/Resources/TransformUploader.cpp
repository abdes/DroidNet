//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/Upload/RingUploadBuffer.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {

[[nodiscard]] auto IsFinite(const glm::mat4& m) noexcept -> bool
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (!std::isfinite(m[c][r])) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

namespace oxygen::renderer::resources {

TransformUploader::TransformUploader(
  Graphics& gfx, const observer_ptr<engine::upload::UploadCoordinator> uploader)
  : gfx_(gfx)
  , uploader_(uploader)
  , worlds_ring_(
      gfx, frame::kFramesInFlight, sizeof(glm::mat4), "WorldTransforms")
  , normals_ring_(
      gfx, frame::kFramesInFlight, sizeof(glm::mat4), "NormalMatrices")
{
  DCHECK_NOTNULL_F(
    uploader_.get(), "TransformUploader requires UploadCoordinator");
}

TransformUploader::~TransformUploader()
{
  auto& registry = gfx_.GetResourceRegistry();
  if (auto buf = worlds_ring_.GetBuffer()) {
    registry.UnRegisterResource(*buf);
  }
  if (auto buf = normals_ring_.GetBuffer()) {
    registry.UnRegisterResource(*buf);
  }

  LOG_SCOPE_F(INFO, "TransformUploader Statistics");
  LOG_F(INFO, "total allocations : {}", total_allocations_);
  LOG_F(INFO, "cache hits        : {}", cache_hits_);
  LOG_F(INFO, "transforms stored : {}", transforms_.size());

  {
    LOG_SCOPE_F(INFO, "Worlds Ring Buffer");
    worlds_ring_.LogTelemetryStats();
  }
  {
    LOG_SCOPE_F(INFO, "Normals Ring Buffer");
    normals_ring_.LogTelemetryStats();
  }
}

auto TransformUploader::OnFrameStart(oxygen::frame::Slot slot) -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) {
    current_epoch_ = 1U;
  }

  // Clear cache only when we complete a full cycle (same slot as cache
  // creation)
  const bool should_clear_cache
    = cache_creation_slot_.has_value() && cache_creation_slot_.value() == slot;

  if (should_clear_cache || key_to_handle_.empty()) {
    // Clear per-frame state
    transforms_.clear();
    normal_matrices_.clear();
    key_to_handle_.clear();
    cache_creation_slot_ = slot; // Record new cache creation slot
  }

  uploaded_this_frame_ = false;

  // Set active partitions in ring buffers
  worlds_ring_.SetActivePartition(slot);
  normals_ring_.SetActivePartition(slot);
}

auto TransformUploader::GetOrAllocate(const glm::mat4& transform)
  -> engine::sceneprep::TransformHandle
{
  DCHECK_F(IsFinite(transform), "GetOrAllocate received non-finite matrix");

  // Check for existing transform using hash-based deduplication
  const auto key = MakeTransformKey(transform);
  if (const auto it = key_to_handle_.find(key); it != key_to_handle_.end()) {
    ++cache_hits_;
    return it->second;
  }

  // Store transform locally
  transforms_.push_back(transform);
  normal_matrices_.push_back(ComputeNormalMatrix(transform));

  // Ensure ring buffers have capacity for one more element
  (void)worlds_ring_.ReserveElements(transforms_.size(), 0.5f);
  (void)normals_ring_.ReserveElements(transforms_.size(), 0.5f);

  // Allocate space in ring buffers - this gives us the absolute handle
  const auto world_alloc = worlds_ring_.Allocate(1);
  const auto normal_alloc = normals_ring_.Allocate(1);

  DCHECK_F(world_alloc.has_value(), "Failed to allocate world transform");
  DCHECK_F(normal_alloc.has_value(), "Failed to allocate normal matrix");

  // Handle is the absolute index from the ring buffer
  const auto handle = engine::sceneprep::TransformHandle {
    static_cast<std::uint32_t>(world_alloc->first_index)
  };

  // Cache for deduplication
  key_to_handle_[key] = handle;
  ++total_allocations_;
  return handle;
}

auto TransformUploader::IsValidHandle(
  engine::sceneprep::TransformHandle handle) const -> bool
{
  // Check if the handle corresponds to an allocated transform
  // Since we clear state each frame, check if handle matches any cached handle
  for (const auto& [key, cached_handle] : key_to_handle_) {
    if (cached_handle.get() == handle.get()) {
      return true;
    }
  }
  return false;
}

auto TransformUploader::EnsureFrameResources() -> void
{
  if (uploaded_this_frame_ || transforms_.empty()) {
    return;
  }

  // Upload all transform data that was allocated during this frame
  const auto all_world_bytes = std::as_bytes(
    std::span<const glm::mat4>(transforms_.data(), transforms_.size()));
  const auto all_normal_bytes = std::as_bytes(
    std::span<const glm::mat4>(normal_matrices_.data(), transforms_.size()));

  // Use the ring buffer's new method to upload all allocated data
  if (const auto world_req = worlds_ring_.MakeUploadRequestForAllocatedRange(
        all_world_bytes, "WorldTransforms")) {
    uploader_->SubmitMany(std::span { &*world_req, 1 });
  }

  if (const auto normal_req = normals_ring_.MakeUploadRequestForAllocatedRange(
        all_normal_bytes, "NormalMatrices")) {
    uploader_->SubmitMany(std::span { &*normal_req, 1 });
  }

  uploaded_this_frame_ = true;
}

auto TransformUploader::GetWorldsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(
    worlds_ring_.GetBuffer(), "EnsureFrameResources() must be called first");
  return worlds_ring_.GetBindlessIndex();
}

auto TransformUploader::GetNormalsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(
    normals_ring_.GetBuffer(), "EnsureFrameResources() must be called first");
  return normals_ring_.GetBindlessIndex();
}

auto TransformUploader::GetWorldMatrices() const noexcept
  -> std::span<const glm::mat4>
{
  return transforms_;
}

auto TransformUploader::GetNormalMatrices() const noexcept
  -> std::span<const glm::mat4>
{
  return normal_matrices_;
}

auto TransformUploader::ComputeNormalMatrix(const glm::mat4& world) noexcept
  -> glm::mat4
{
  // Extract upper-left 3x3
  const float a00 = world[0][0], a01 = world[0][1], a02 = world[0][2];
  const float a10 = world[1][0], a11 = world[1][1], a12 = world[1][2];
  const float a20 = world[2][0], a21 = world[2][1], a22 = world[2][2];

  // Compute determinant
  const float det = a00 * (a11 * a22 - a12 * a21)
    - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);

  constexpr float kDetEps = 1e-12f;
  if (!std::isfinite(det) || std::fabs(det) <= kDetEps) {
    return glm::mat4 { 1.0f };
  }

  const float inv_det = 1.0f / det;

  // Inverse of 3x3 (cofactor matrix transposed)
  const float i00 = (a11 * a22 - a12 * a21) * inv_det;
  const float i01 = (a02 * a21 - a01 * a22) * inv_det;
  const float i02 = (a01 * a12 - a02 * a11) * inv_det;
  const float i10 = (a12 * a20 - a10 * a22) * inv_det;
  const float i11 = (a00 * a22 - a02 * a20) * inv_det;
  const float i12 = (a02 * a10 - a00 * a12) * inv_det;
  const float i20 = (a10 * a21 - a11 * a20) * inv_det;
  const float i21 = (a01 * a20 - a00 * a21) * inv_det;
  const float i22 = (a00 * a11 - a01 * a10) * inv_det;

  // Transpose to get inverse-transpose
  glm::mat4 result { 1.0f };
  result[0][0] = i00;
  result[0][1] = i10;
  result[0][2] = i20;
  result[1][0] = i01;
  result[1][1] = i11;
  result[1][2] = i21;
  result[2][0] = i02;
  result[2][1] = i12;
  result[2][2] = i22;
  return result;
}

auto TransformUploader::MakeTransformKey(const glm::mat4& m) noexcept
  -> std::uint64_t
{
  // Quantize the first 3 rows of each column (12 floats total)
  constexpr float scale = 1024.0f;
  std::array<std::int32_t, 12> quantized;

  auto quantized_view = std::views::iota(0, 4) // columns
    | std::views::transform([&](const int c) {
        return std::views::iota(0, 3) | std::views::transform([&](const int r) {
          return static_cast<std::int32_t>(std::lround(m[c][r] * scale));
        });
      })
    | std::views::join;

  std::ranges::copy(quantized_view, quantized.begin());
  return oxygen::ComputeFNV1a64(quantized.data(), sizeof(quantized));
}

auto TransformUploader::MatrixAlmostEqual(
  const glm::mat4& a, const glm::mat4& b, const float eps) noexcept -> bool
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      const float av = a[c][r];
      const float bv = b[c][r];
      const float diff = std::fabs(av - bv);
      if (diff <= eps) {
        continue;
      }
      const float max_abs = std::max(std::fabs(av), std::fabs(bv));
      if (diff <= eps * std::max(1.0f, max_abs)) {
        continue;
      }
      return false;
    }
  }
  return true;
}

} // namespace oxygen::renderer::resources
