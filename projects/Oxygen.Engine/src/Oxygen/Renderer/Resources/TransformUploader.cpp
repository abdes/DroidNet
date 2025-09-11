//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/Resources/UploadHelpers.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <cmath>
#include <cstring>

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

// Quantized key helper
// Quantize the upper-left 3x4 of the transform and compute a stable 64-bit
// hash. This reduces sensitivity to tiny floating point noise while still
// hashing the meaningful components (rotation, scale, shear, translation).
auto MakeTransformKey(const glm::mat4& m) noexcept -> std::uint64_t
{
  // Quantize the first 3 rows of each column into 12 ints.
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

// Element-wise almost-equality for matrices. Uses absolute + relative
// tolerance to be robust across scales.
auto MatrixAlmostEqual(const glm::mat4& a, const glm::mat4& b,
  const float eps = 1e-5f) noexcept -> bool
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

} // namespace

namespace oxygen::renderer::resources {

TransformUploader::TransformUploader(
  Graphics& gfx, const observer_ptr<engine::upload::UploadCoordinator> uploader)
  : gfx_(gfx)
  , uploader_(uploader)
{
}

TransformUploader::~TransformUploader()
{
  // Best-effort cleanup: unregister our GPU buffers from the registry so they
  // don't linger until registry destruction.
  auto& registry = gfx_.GetResourceRegistry();
  if (gpu_world_buffer_) {
    registry.UnRegisterResource(*gpu_world_buffer_);
    gpu_world_buffer_.reset();
  }
  if (gpu_normals_buffer_) {
    registry.UnRegisterResource(*gpu_normals_buffer_);
    gpu_normals_buffer_.reset();
  }
}

auto TransformUploader::GetOrAllocate(const glm::mat4& transform)
  -> engine::sceneprep::TransformHandle
{
  DCHECK_F(
    IsFinite(transform), "GetOrAllocate received non-finite matrix values");
  const auto key = MakeTransformKey(transform);
  if (const auto it = transform_key_to_handle_.find(key);
    it != transform_key_to_handle_.end()) {
    const auto h = it->second;
    const auto idx = static_cast<std::size_t>(h.get());
    if (idx < transforms_.size()
      && MatrixAlmostEqual(transforms_[idx], transform)) {
      return h; // accept near-equal matrices
    }
  }
  // Not found or collision mismatch: allocate new handle
  // Prefer reuse from free list when available
  engine::sceneprep::TransformHandle handle;
  if (!free_handles_.empty()) {
    const auto idx = free_handles_.back();
    free_handles_.pop_back();
    handle = engine::sceneprep::TransformHandle { idx };
  } else {
    handle = next_handle_;
    next_handle_
      = engine::sceneprep::TransformHandle { next_handle_.get() + 1U };
  }
  const auto idx = static_cast<std::size_t>(handle.get());
  if (transforms_.size() <= idx) {
    transforms_.resize(idx + 1U);
    normal_matrices_.resize(idx + 1U);
    world_versions_.resize(idx + 1U);
    normal_versions_.resize(idx + 1U);
    dirty_epoch_.resize(idx + 1U);
    index_to_key_.resize(idx + 1U, std::numeric_limits<std::uint64_t>::max());
  }
  transforms_[idx] = transform;
  // Compute normal matrix (inverse transpose upper-left 3x3) lazily here.
  normal_matrices_[idx] = ComputeNormalMatrix(transform);
  world_versions_[idx] = global_version_;
  normal_versions_[idx] = global_version_;
  // Mark dirty for this frame.
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
  transform_key_to_handle_[key] = handle;
  index_to_key_[idx] = key;

  return handle;
}

auto TransformUploader::Update(
  engine::sceneprep::TransformHandle handle, const glm::mat4& transform) -> void
{
  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < transforms_.size(),
    "Update received invalid handle index {} (size={})", idx,
    transforms_.size());
  if (transforms_[idx] == transform) {
    return; // no change
  }
  ++global_version_;
  transforms_[idx] = transform;
  normal_matrices_[idx] = ComputeNormalMatrix(transform);
  world_versions_[idx] = global_version_;
  normal_versions_[idx] = global_version_;
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
}

auto TransformUploader::OnFrameStart() -> void
{
  // BeginFrame must be called once per frame by the orchestrator (Renderer).
  ++current_epoch_;
  if (current_epoch_ == 0U) { // wrapped
    current_epoch_ = 1U;
    std::ranges::fill(dirty_epoch_, 0U);
  }
  dirty_indices_.clear();
  uploaded_ = false;
}

// Accessors for spans and dirty indices retained; individual element getters
// removed.

auto TransformUploader::GetWorldMatrices() const noexcept
  -> std::span<const glm::mat4>
{
  return { transforms_.data(), transforms_.size() };
}

auto TransformUploader::GetNormalMatrices() const noexcept
  -> std::span<const glm::mat4>
{
  return { normal_matrices_.data(), normal_matrices_.size() };
}

auto TransformUploader::GetDirtyIndices() const noexcept
  -> std::span<const std::uint32_t>

{
  return { dirty_indices_.data(), dirty_indices_.size() };
}

auto TransformUploader::IsValidHandle(
  engine::sceneprep::TransformHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < transforms_.size();
}

auto TransformUploader::ComputeNormalMatrix(const glm::mat4& world) noexcept
  -> glm::mat4
{
  // Compute inverse-transpose of the upper-left 3x3, place into a mat4.
  // Keep translation as (0,0,0,1).
  // glm::inverseTranspose(mat3) may not be available consistently; do it
  // explicitly for robustness.
  // Extract 3x3
  const float a00 = world[0][0], a01 = world[0][1], a02 = world[0][2];
  const float a10 = world[1][0], a11 = world[1][1], a12 = world[1][2];
  const float a20 = world[2][0], a21 = world[2][1], a22 = world[2][2];

  // Compute determinant
  const float det = a00 * (a11 * a22 - a12 * a21)
    - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);

  // Guard against singular matrices; fall back to identity normal matrix.
  constexpr float kDetEps = 1e-12f;
  if (!std::isfinite(det) || std::fabs(det) <= kDetEps) {
    return glm::mat4 { 1.0f };
  }
  const float inv_det = 1.0f / det;

  // Inverse of 3x3 (cofactor matrix transposed) times inv_det
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
  glm::mat4 n { 1.0f };
  n[0][0] = i00;
  n[0][1] = i10;
  n[0][2] = i20;
  n[0][3] = 0.0f;
  n[1][0] = i01;
  n[1][1] = i11;
  n[1][2] = i21;
  n[1][3] = 0.0f;
  n[2][0] = i02;
  n[2][1] = i12;
  n[2][2] = i22;
  n[2][3] = 0.0f;
  n[3][0] = 0.0f;
  n[3][1] = 0.0f;
  n[3][2] = 0.0f;
  n[3][3] = 1.0f;
  return n;
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto TransformUploader::EnsureBufferAndSrv(
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  const std::uint64_t size_bytes, const char* debug_label) -> bool
{
  return internal::EnsureBufferAndSrv(gfx_, buffer, bindless_index, size_bytes,
    sizeof(glm::mat4), std::string_view(debug_label));
}

auto TransformUploader::Release(engine::sceneprep::TransformHandle handle)
  -> void
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx >= transforms_.size()) {
    DLOG_F(2, "TransformUploader::Release called with invalid handle {}", idx);
    return;
  }
  // O(1) removal: look up the key for this index and erase mapping.
  if (idx < index_to_key_.size()) {
    const auto key = index_to_key_[idx];
    if (key != std::numeric_limits<std::uint64_t>::max()) {
      transform_key_to_handle_.erase(key);
      index_to_key_[idx] = std::numeric_limits<std::uint64_t>::max();
    }
  }

  // Reset transform slot to identity and push to free list for reuse.
  transforms_[idx] = glm::mat4 { 1.0f };
  normal_matrices_[idx] = glm::mat4 { 1.0f };
  world_versions_[idx] = 0U;
  normal_versions_[idx] = 0U;
  dirty_epoch_[idx] = 0U;
  free_handles_.push_back(static_cast<std::uint32_t>(idx));
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto TransformUploader::BuildSparseUploadRequests(
  const std::vector<std::uint32_t>& indices,
  const std::span<const glm::mat4> src,
  const std::shared_ptr<graphics::Buffer>& dst, const char* debug_name) const
  -> std::vector<engine::upload::UploadRequest>
{
  std::vector<engine::upload::UploadRequest> uploads;
  if (!dst || indices.empty()) {
    return uploads;
  }

  std::vector<std::uint32_t> sorted = indices;
  std::ranges::sort(sorted);
  sorted.erase(std::ranges::unique(sorted).begin(), sorted.end());

  uploads.reserve(sorted.size());
  std::size_t run_begin = 0;
  while (run_begin < sorted.size()) {
    std::size_t run_end = run_begin;
    while (run_end + 1 < sorted.size()
      && sorted[run_end + 1] == (sorted[run_end] + 1)) {
      ++run_end;
    }
    const std::uint32_t first = sorted[run_begin];
    const std::uint32_t last = sorted[run_end];
    const std::size_t count = last - first + 1;
    const std::size_t byte_count = count * sizeof(glm::mat4);
    const std::size_t byte_offset
      = static_cast<std::size_t>(first) * sizeof(glm::mat4);

    engine::upload::UploadRequest r;
    r.kind = engine::upload::UploadKind::kBuffer;
    r.batch_policy = engine::upload::BatchPolicy::kCoalesce;
    r.debug_name = debug_name;
    r.desc = engine::upload::UploadBufferDesc {
      .dst = dst,
      .size_bytes = static_cast<std::uint64_t>(byte_count),
      .dst_offset = static_cast<std::uint64_t>(byte_offset),
    };
    const auto* src_ptr = reinterpret_cast<const std::byte*>(&src[first]);
    r.data = engine::upload::UploadDataView { std::span<const std::byte>(
      src_ptr, byte_count) };
    uploads.emplace_back(std::move(r));

    run_begin = run_end + 1;
  }

  return uploads;
}

auto TransformUploader::UploadWorldMatrices() -> void
{
  const uint64_t buffer_size = transforms_.size() * sizeof(glm::mat4);
  if (buffer_size == 0) {
    return;
  }

  // Create or resize the device-local buffer when needed (DRY helper)
  const auto prev_world_index = worlds_bindless_index_;
  const bool need_recreate = EnsureBufferAndSrv(
    gpu_world_buffer_, worlds_bindless_index_, buffer_size, "WorldTransforms");

  // Stability check: once assigned, the bindless index should remain stable
  // across frames. Skip the check when we recreated the buffer/SRV.
  if (!need_recreate && prev_world_index != kInvalidShaderVisibleIndex) {
    DCHECK_F(worlds_bindless_index_ == prev_world_index,
      "Worlds bindless index changed from {} to {} unexpectedly",
      prev_world_index, worlds_bindless_index_);
  }

  // Upload current CPU data
  if (uploader_ && gpu_world_buffer_) {
    // If buffer was recreated or the dirty region set is large, do a full
    // upload.
    const bool do_full_upload = need_recreate
      || (dirty_indices_.size() * sizeof(glm::mat4) > (buffer_size / 2));

    if (do_full_upload) {
      engine::upload::UploadRequest req;
      req.kind = engine::upload::UploadKind::kBuffer;
      req.batch_policy = engine::upload::BatchPolicy::kCoalesce;
      req.debug_name = "WorldTransforms";
      req.desc = engine::upload::UploadBufferDesc {
        .dst = gpu_world_buffer_,
        .size_bytes = buffer_size,
        .dst_offset = 0,
      };
      req.data = engine::upload::UploadDataView { std::as_bytes(
        std::span<const glm::mat4>(transforms_)) };
      static_cast<void>(uploader_->SubmitMany(std::span { &req, 1 }));
    } else if (!dirty_indices_.empty()) {
      auto uploads = BuildSparseUploadRequests(dirty_indices_,
        std::span<const glm::mat4>(transforms_), gpu_world_buffer_,
        "WorldTransforms.sparse");
      if (!uploads.empty()) {
        static_cast<void>(uploader_->SubmitMany(uploads));
      }
    }
  }
}

auto TransformUploader::UploadNormalMatrices() -> void
{
  const uint64_t buffer_size = normal_matrices_.size() * sizeof(glm::mat4);
  if (buffer_size == 0) {
    return;
  }

  const auto prev_normal_index = normals_bindless_index_;
  const bool need_recreate = EnsureBufferAndSrv(gpu_normals_buffer_,
    normals_bindless_index_, buffer_size, "NormalMatrices");

  if (!need_recreate && prev_normal_index != kInvalidShaderVisibleIndex) {
    DCHECK_F(normals_bindless_index_ == prev_normal_index,
      "Normals bindless index changed from {} to {} unexpectedly",
      prev_normal_index, normals_bindless_index_);
  }

  if (uploader_ && gpu_normals_buffer_) {
    const bool do_full_upload = need_recreate
      || (dirty_indices_.size() * sizeof(glm::mat4) > (buffer_size / 2));

    if (do_full_upload) {
      engine::upload::UploadRequest req;
      req.kind = engine::upload::UploadKind::kBuffer;
      req.batch_policy = engine::upload::BatchPolicy::kCoalesce;
      req.debug_name = "NormalMatrices";
      req.desc = engine::upload::UploadBufferDesc {
        .dst = gpu_normals_buffer_,
        .size_bytes = buffer_size,
        .dst_offset = 0,
      };
      req.data = engine::upload::UploadDataView { std::as_bytes(
        std::span<const glm::mat4>(normal_matrices_)) };
      static_cast<void>(uploader_->SubmitMany(std::span { &req, 1 }));
    } else if (!dirty_indices_.empty()) {
      auto uploads = BuildSparseUploadRequests(dirty_indices_,
        std::span<const glm::mat4>(normal_matrices_), gpu_normals_buffer_,
        "NormalMatrices.sparse");
      if (!uploads.empty()) {
        static_cast<void>(uploader_->SubmitMany(uploads));
      }
    }
  }
}

auto TransformUploader::GetNormalsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(gpu_normals_buffer_,
    "EnsureFrameResources() must be called before GetNormalsSrvIndex()");
  return normals_bindless_index_;
}

auto TransformUploader::GetWorldsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(gpu_world_buffer_,
    "EnsureFrameResources() must be called before GetWorldsSrvIndex()");
  return worlds_bindless_index_;
}

auto TransformUploader::EnsureFrameResources() -> void
{
  if (uploaded_) {
    return; // already uploaded this frame
  }

  // Contract: BeginFrame() must have been called this frame
  DCHECK_F(current_epoch_ > 0U,
    "EnsureFrameResources() called before BeginFrame() - frame lifecycle "
    "violation");

  UploadWorldMatrices();
  UploadNormalMatrices();
  uploaded_ = true;
}

} // namespace oxygen::renderer::resources
