//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>

#include <glm/mat4x4.hpp>

#include <limits>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

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

TransformUploader::TransformUploader(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::StagingProvider> provider,
  observer_ptr<engine::upload::InlineTransfersCoordinator> inline_transfers)
  : gfx_(std::move(gfx))
  , staging_provider_(std::move(provider))
  , inline_transfers_(inline_transfers)
  , worlds_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::mat4)), inline_transfers_,
      "TransformUploader.Worlds")
  , normals_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::mat4)), inline_transfers_,
      "TransformUploader.Normals")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
  DCHECK_NOTNULL_F(
    inline_transfers_, "TransformUploader requires InlineTransfersCoordinator");
}

TransformUploader::~TransformUploader()
{
  LOG_SCOPE_F(INFO, "TransformUploader Statistics");
  LOG_F(INFO, "total calls       : {}", total_calls_);
  LOG_F(INFO, "total allocations : {}", total_allocations_);
  LOG_F(INFO, "cache hits        : {}", cache_hits_);
  LOG_F(INFO, "transforms stored : {}", transforms_.size());
}

auto TransformUploader::OnFrameStart(renderer::RendererTag,
  oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) {
    current_epoch_ = 1U;
  }
  frame_write_count_ = 0U;

  worlds_buffer_.OnFrameStart(sequence, slot);
  normals_buffer_.OnFrameStart(sequence, slot);

  // Clear cached SRV indices; they will be renewed during
  // EnsureFrameResources()
  worlds_srv_index_ = kInvalidShaderVisibleIndex;
  normals_srv_index_ = kInvalidShaderVisibleIndex;

  // Phase 1: Keep cache across frames for stable element indices.
  uploaded_this_frame_ = false;
}

auto TransformUploader::GetOrAllocate(const glm::mat4& transform)
  -> engine::sceneprep::TransformHandle
{
  ++total_calls_;

  DCHECK_F(IsFinite(transform), "GetOrAllocate received non-finite matrix");

  // Check for existing transform using hash-based deduplication. We first
  // compute a quantized key. If the key exists we verify that the actual
  // stored matrix is nearly equal to avoid false positives from quantization
  // or hash collisions.
  const auto key = MakeTransformKey(transform);
  if (const auto it = key_to_handle_.find(key); it != key_to_handle_.end()) {
    const auto& entry = it->second;
    const auto cached_handle = entry.handle;
    const auto index = entry.index;
    if (index < transforms_.size()) {
      const auto& stored = transforms_[index];
      if (MatrixAlmostEqual(stored, transform)) {
        ++cache_hits_;
        // Cache hit: keep CPU-side storage consistent without marking dirty.
        // Keep CPU-side storage consistent, but do not mark dirty if the
        // value is effectively unchanged.
        transforms_[index] = transform;
        normal_matrices_[index] = ComputeNormalMatrix(transform);
        // Consume one write slot this frame to keep order stable.
        ++frame_write_count_;
        return cached_handle;
      }
      // Treat as same logical transform whose value changed; update in-place
      // to keep handle stable.
      transforms_[index] = transform;
      normal_matrices_[index] = ComputeNormalMatrix(transform);
      // Rebind new key to same handle for any subsequent calls this frame.
      key_to_handle_[key] = TransformCacheEntry { cached_handle, index };
      ++frame_write_count_;
      return cached_handle;
    }
  }

  // No cache hit: either a brand new logical transform, or a changed value
  // that should reuse an existing slot this frame. Reuse by frame order.
  const bool is_new_logical = frame_write_count_ >= transforms_.size();
  std::uint32_t index = 0;
  if (is_new_logical) {
    // Append new entries
    transforms_.push_back(transform);
    normal_matrices_.push_back(ComputeNormalMatrix(transform));
    index = static_cast<std::uint32_t>(transforms_.size() - 1);
  } else {
    // Reuse existing slot in this frame by order; update.
    index = frame_write_count_;
    transforms_[index] = transform;
    normal_matrices_[index] = ComputeNormalMatrix(transform);
  }

  // Handle maps 1:1 to index (element index equals insertion order)
  const auto handle = engine::sceneprep::TransformHandle { index };

  // Cache for deduplication (intra-frame): map value key to this handle and
  // the index into CPU arrays used to verify near-equality.
  key_to_handle_[key] = TransformCacheEntry { handle, index };
  if (is_new_logical) {
    ++total_allocations_;
  }
  ++frame_write_count_;
  return handle;
}

auto TransformUploader::IsValidHandle(
  engine::sceneprep::TransformHandle handle) const -> bool
{
  const auto idx = handle.get();
  return idx < transforms_.size();
}

auto TransformUploader::EnsureFrameResources() -> void
{
  if (uploaded_this_frame_ || transforms_.empty()) {
    return;
  }

  const auto count = static_cast<std::uint32_t>(transforms_.size());

  // Allocate transient buffers for this frame
  auto w_res = worlds_buffer_.Allocate(count);
  if (!w_res) {
    LOG_F(ERROR, "Failed to allocate worlds transient buffer: {}",
      w_res.error().message());
    return;
  }
  const auto w_alloc = *w_res;

  auto n_res = normals_buffer_.Allocate(count);
  if (!n_res) {
    LOG_F(ERROR, "Failed to allocate normals transient buffer: {}",
      n_res.error().message());
    return;
  }
  const auto n_alloc = *n_res;

  DLOG_F(1, "TransformUploader writing {} transforms to {}", count,
    fmt::ptr(w_alloc.mapped_ptr));

  // Direct memory write (no upload descriptors needed)
  std::memcpy(w_alloc.mapped_ptr, transforms_.data(),
    transforms_.size() * sizeof(glm::mat4));

  std::memcpy(n_alloc.mapped_ptr, normal_matrices_.data(),
    normal_matrices_.size() * sizeof(glm::mat4));

  // Cache SRV indices for GetWorldsSrvIndex() / GetNormalsSrvIndex()
  worlds_srv_index_ = w_alloc.srv;
  normals_srv_index_ = n_alloc.srv;

  uploaded_this_frame_ = true;
}

auto TransformUploader::GetWorldsSrvIndex() const -> ShaderVisibleIndex
{
  if (worlds_srv_index_ == kInvalidShaderVisibleIndex) {
    // Trigger lazy upload if not yet done
    const_cast<TransformUploader*>(this)->EnsureFrameResources();
  }
  return worlds_srv_index_;
}

auto TransformUploader::GetNormalsSrvIndex() const -> ShaderVisibleIndex
{
  if (normals_srv_index_ == kInvalidShaderVisibleIndex) {
    // Trigger lazy upload if not yet done
    const_cast<TransformUploader*>(this)->EnsureFrameResources();
  }
  return normals_srv_index_;
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
  // Increase scale to reduce accidental collisions for similar-but-different
  // matrices. Balance between memory and sensitivity.
  constexpr float scale = 1024.0f;
  std::array<std::int32_t, 12> quantized;

  // Fill quantized array: for each column (0..3), for each row (0..2)
  int idx = 0;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 3; ++r) {
      quantized[idx++]
        = static_cast<std::int32_t>(std::lround(m[c][r] * scale));
    }
  }

  return oxygen::ComputeFNV1a64(quantized.data(), sizeof(quantized));
}

auto TransformUploader::MatrixAlmostEqual(
  const glm::mat4& a, const glm::mat4& b) noexcept -> bool
{
  // Use standard float epsilon as the base.
  // std::numeric_limits<float>::epsilon() is ~1.19e-7; scale it up to be
  // comparable to the previous default of 1e-5.
  constexpr float kEps
    = std::numeric_limits<float>::epsilon() * 100.0f; // ~1.19e-5

  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      const float av = a[c][r];
      const float bv = b[c][r];
      const float diff = std::fabs(av - bv);
      if (diff <= kEps) {
        continue;
      }
      const float max_abs = std::max(std::fabs(av), std::fabs(bv));
      if (diff <= kEps * std::max(1.0f, max_abs)) {
        continue;
      }
      return false;
    }
  }
  return true;
}

} // namespace oxygen::renderer::resources
