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
#include <Oxygen/Renderer/Upload/RingBufferStaging.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

using oxygen::engine::upload::AtlasBuffer;

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
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> provider)
  : gfx_(std::move(gfx))
  , uploader_(std::move(uploader))
  , staging_provider_(std::move(provider))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  // Prepare atlas buffers (not yet used for uploads in Phase 1 wiring step)
  worlds_atlas_ = std::make_unique<AtlasBuffer>(gfx_,
    static_cast<std::uint32_t>(sizeof(glm::mat4)), "WorldTransformsAtlas");
  normals_atlas_ = std::make_unique<AtlasBuffer>(
    gfx_, static_cast<std::uint32_t>(sizeof(glm::mat4)), "NormalMatricesAtlas");
}

TransformUploader::~TransformUploader()
{
  LOG_SCOPE_F(INFO, "TransformUploader Statistics");
  LOG_F(INFO, "total calls       : {}", total_calls_);
  LOG_F(INFO, "total allocations : {}", total_allocations_);
  LOG_F(INFO, "cache hits        : {}", cache_hits_);
  LOG_F(INFO, "atlas allocations : {}", atlas_allocations_);
  LOG_F(INFO, "upload operations : {}", upload_operations_);
  LOG_F(INFO, "transforms stored : {}", transforms_.size());

  if (worlds_atlas_) {
    const auto ws = worlds_atlas_->GetStats();
    LOG_SCOPE_F(INFO, "Worlds Atlas Buffer");
    LOG_F(INFO, "ensure calls      : {}", ws.ensure_calls);
    LOG_F(INFO, "allocations       : {}", ws.allocations);
    LOG_F(INFO, "releases          : {}", ws.releases);
    LOG_F(INFO, "capacity elements : {}", ws.capacity_elements);
    LOG_F(INFO, "next index        : {}", ws.next_index);
    LOG_F(INFO, "free list size    : {}", ws.free_list_size);
  }

  if (normals_atlas_) {
    const auto ns = normals_atlas_->GetStats();
    LOG_SCOPE_F(INFO, "Normals Atlas Buffer");
    LOG_F(INFO, "ensure calls      : {}", ns.ensure_calls);
    LOG_F(INFO, "allocations       : {}", ns.allocations);
    LOG_F(INFO, "releases          : {}", ns.releases);
    LOG_F(INFO, "capacity elements : {}", ns.capacity_elements);
    LOG_F(INFO, "next index        : {}", ns.next_index);
    LOG_F(INFO, "free list size    : {}", ns.free_list_size);
  }
}

auto TransformUploader::OnFrameStart(
  renderer::RendererTag, oxygen::frame::Slot slot) -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) {
    current_epoch_ = 1U;
  }
  frame_write_count_ = 0U;
  current_frame_slot_ = slot;
  // Phase 1: Keep cache across frames for stable element indices.
  // Cache clearing violated Phase 1 "stable entries" requirement.

  // IMPORTANT: Do NOT clear the deduplication map based on frame slots!
  // Clearing key_to_handle_ breaks handle stability and causes transforms
  // to get new handles each cycle. The deduplication map should only be
  // cleared for intra-frame deduplication, not cross-frame stability.

  uploaded_this_frame_ = false;
  // Phase 1 (atlas path): we do not track first_new_index_. We upload
  // all cached transforms every frame and keep indices stable.

  // Prepare atlases lifecycle (recycle any retired elements for this slot)
  if (worlds_atlas_) {
    worlds_atlas_->OnFrameStart(slot);
  }
  if (normals_atlas_) {
    normals_atlas_->OnFrameStart(slot);
  }
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
      // to keep handle stable and mark dirty for this frame.
      transforms_[index] = transform;
      normal_matrices_[index] = ComputeNormalMatrix(transform);
      if (index >= dirty_epoch_.size()) {
        dirty_epoch_.resize(index + 1, 0U);
      }
      dirty_epoch_[index] = current_epoch_;
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
    dirty_epoch_.push_back(current_epoch_);
    index = static_cast<std::uint32_t>(transforms_.size() - 1);
  } else {
    // Reuse existing slot in this frame by order; mark dirty and update.
    index = frame_write_count_;
    transforms_[index] = transform;
    normal_matrices_[index] = ComputeNormalMatrix(transform);
    if (index >= dirty_epoch_.size()) {
      dirty_epoch_.resize(index + 1, 0U);
    }
    dirty_epoch_[index] = current_epoch_;
  }
  // Note: first_new_index_ points to the first element for this frame; no
  // change needed here.
  // Ensure atlas capacity and allocate one element per kind
  // Ensure atlas capacity to hold current logical count
  if (const auto result = worlds_atlas_->EnsureCapacity(
        static_cast<std::uint32_t>(transforms_.size()), 0.5f);
    !result) {
    LOG_F(ERROR, "Failed to ensure world atlas capacity: {}",
      result.error().message());
    return engine::sceneprep::kInvalidTransformHandle;
  }
  if (const auto result = normals_atlas_->EnsureCapacity(
        static_cast<std::uint32_t>(transforms_.size()), 0.5f);
    !result) {
    LOG_F(ERROR, "Failed to ensure normal atlas capacity: {}",
      result.error().message());
    return engine::sceneprep::kInvalidTransformHandle;
  }

  // Ensure element refs arrays are sized; allocate refs only when new
  if (world_refs_.size() < transforms_.size()) {
    const auto need
      = static_cast<std::uint32_t>(transforms_.size() - world_refs_.size());
    for (std::uint32_t n = 0; n < need; ++n) {
      auto wref = worlds_atlas_->Allocate(1);
      auto nref = normals_atlas_->Allocate(1);
      DCHECK_F(wref.has_value(), "Failed to allocate world transform (atlas)");
      DCHECK_F(nref.has_value(), "Failed to allocate normal matrix (atlas)");
      world_refs_.push_back(*wref);
      normal_refs_.push_back(*nref);
      ++atlas_allocations_; // Count logical transform allocation (world+normal
                            // pair)
    }
  }

  // Handle maps 1:1 to index (element index equals insertion order)
  const auto handle = engine::sceneprep::TransformHandle { index };
  // Mapping from logical handle to atlas element index is stable per-frame.

  // Cache for deduplication (intra-frame): map value key to this handle and
  // the index into CPU arrays used to verify near-equality.
  std::uint32_t mapped_index = index;

  // Release old atlas allocation if we're replacing an existing cache entry
  if (auto it = key_to_handle_.find(key); it != key_to_handle_.end()) {
    const auto old_index = it->second.index;
    if (old_index < world_refs_.size()) {
      worlds_atlas_->Release(world_refs_[old_index], current_frame_slot_);
      normals_atlas_->Release(normal_refs_[old_index], current_frame_slot_);
    }
  }

  key_to_handle_[key] = TransformCacheEntry { handle, mapped_index };
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
  // Ensure SRVs exist even if there are no new uploads this frame
  if (const auto result = worlds_atlas_->EnsureCapacity(
        std::max<std::uint32_t>(
          1U, static_cast<std::uint32_t>(transforms_.size())),
        0.5f);
    !result) {
    LOG_F(ERROR,
      "Failed to ensure world atlas capacity for frame resources: {}",
      result.error().message());
    return; // Skip uploads if capacity cannot be ensured
  }
  if (const auto result = normals_atlas_->EnsureCapacity(
        std::max<std::uint32_t>(
          1U, static_cast<std::uint32_t>(transforms_.size())),
        0.5f);
    !result) {
    LOG_F(ERROR,
      "Failed to ensure normal atlas capacity for frame resources: {}",
      result.error().message());
    return; // Skip uploads if capacity cannot be ensured
  }
  std::vector<oxygen::engine::upload::UploadRequest> requests;
  requests.reserve(8); // small, will grow if needed

  const auto stride = static_cast<std::uint64_t>(sizeof(glm::mat4));
  const auto count = static_cast<std::uint32_t>(transforms_.size());

  // Emit naive per-element requests for dirty entries; coordinator will batch
  // and coalesce (including contiguous merges) across all buffer requests.
  // Phase 1: Upload all transforms every frame (ignore dirty tracking)
  for (std::uint32_t i = 0; i < count; ++i) {
    // Phase 1 behavior: Upload all transforms every frame regardless of dirty
    // status

    // const bool is_dirty = (i < dirty_epoch_.size()) &&
    // (dirty_epoch_[i] == current_epoch_); if (!is_dirty) {
    //   continue;
    // }

    // World matrix
    if (auto desc = worlds_atlas_->MakeUploadDesc(world_refs_[i], stride)) {
      oxygen::engine::upload::UploadRequest req;
      req.kind = oxygen::engine::upload::UploadKind::kBuffer;
      req.debug_name = "WorldTransform";
      req.desc = *desc;
      req.data = oxygen::engine::upload::UploadDataView { std::as_bytes(
        std::span<const glm::mat4>(&transforms_[i], 1)) };
      requests.push_back(std::move(req));
    } else {
      LOG_F(
        ERROR, "Failed to create upload descriptor for world transform {}", i);
    }

    // Normal matrix
    if (auto desc = normals_atlas_->MakeUploadDesc(normal_refs_[i], stride)) {
      oxygen::engine::upload::UploadRequest req;
      req.kind = oxygen::engine::upload::UploadKind::kBuffer;
      req.debug_name = "NormalMatrix";
      req.desc = *desc;
      req.data = oxygen::engine::upload::UploadDataView { std::as_bytes(
        std::span<const glm::mat4>(&normal_matrices_[i], 1)) };
      requests.push_back(std::move(req));
    } else {
      LOG_F(
        ERROR, "Failed to create upload descriptor for normal matrix {}", i);
    }
  }

  if (!requests.empty()) {
    const auto tickets = uploader_->SubmitMany(
      std::span { requests.data(), requests.size() }, *staging_provider_);
    upload_operations_ += requests.size(); // Track number of upload operations

    // Handle upload submission result
    if (!tickets.has_value()) {
      std::error_code ec = tickets.error();
      LOG_F(ERROR, "Transform upload submission failed: [{}] {}",
        ec.category().name(), ec.message());
      uploaded_this_frame_ = true; // Mark as uploaded to prevent retry loops
      return;
    }

    auto& ticket_vector = tickets.value();
    // Validate upload tickets - if fewer tickets returned than requested, some
    // failed
    if (ticket_vector.size() != requests.size()) {
      LOG_F(ERROR,
        "Transform upload submission failed: expected {} tickets, got {}. {} "
        "uploads failed.",
        requests.size(), ticket_vector.size(),
        requests.size() - ticket_vector.size());
    }
  }
  // Leave resident; only dirty parts updated.

  uploaded_this_frame_ = true;
}

auto TransformUploader::GetWorldsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(worlds_atlas_.get(), "Atlas not initialized");
  // Lazily ensure SRV is created before returning the bindless index
  if (worlds_atlas_->GetBinding().srv == ShaderVisibleIndex {}) {
    if (const auto result = worlds_atlas_->EnsureCapacity(
          std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(transforms_.size())),
          0.5f);
      !result) {
      LOG_F(ERROR, "Failed to ensure world atlas capacity for SRV creation: {}",
        result.error().message());
      return kInvalidShaderVisibleIndex;
    }
  }
  return worlds_atlas_->GetBinding().srv;
}

auto TransformUploader::GetNormalsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(normals_atlas_.get(), "Atlas not initialized");
  if (normals_atlas_->GetBinding().srv == kInvalidShaderVisibleIndex) {
    if (const auto result = normals_atlas_->EnsureCapacity(
          std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(transforms_.size())),
          0.5f);
      !result) {
      LOG_F(ERROR,
        "Failed to ensure normal atlas capacity for SRV creation: {}",
        result.error().message());
      return kInvalidShaderVisibleIndex;
    }
  }
  return normals_atlas_->GetBinding().srv;
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
