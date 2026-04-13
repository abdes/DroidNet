//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Transforms/IsFinite.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/TransformUploader.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::resources {
namespace {

  constexpr oxygen::nexus::DomainKey kTransformDomain {
    .domain = oxygen::bindless::generated::kGlobalSrvDomain,
  };

} // namespace

TransformUploader::TransformUploader(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , slot_reuse_(
      [this](oxygen::nexus::DomainKey /*domain*/) -> bindless::HeapIndex {
        return bindless::HeapIndex { frame_write_count_ };
      },
      [](oxygen::nexus::DomainKey /*domain*/,
        bindless::HeapIndex /*index*/) -> void {},
      slot_reclaimer_)
  , worlds_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::mat4)), inline_transfers_,
      "TransformUploader.Worlds")
  , normals_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::mat4)), inline_transfers_,
      "TransformUploader.Normals")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

TransformUploader::~TransformUploader()
{
  const auto telemetry = slot_reuse_.GetTelemetrySnapshot();
  const auto expected_zero_marker = [](const uint64_t value) -> const char* {
    return value == 0U ? " ✓" : " (expected 0) !";
  };

  LOG_SCOPE_F(INFO, "TransformUploader Statistics");
  LOG_F(INFO, "frames started            : {}", frames_started_count_);
  LOG_F(INFO, "nexus.allocate_calls      : {}", telemetry.allocate_calls);
  LOG_F(INFO, "nexus.release_calls       : {}{}", telemetry.release_calls,
    expected_zero_marker(telemetry.release_calls));
  LOG_F(INFO, "nexus.stale_reject_count  : {}{}", telemetry.stale_reject_count,
    expected_zero_marker(telemetry.stale_reject_count));
  LOG_F(INFO, "nexus.duplicate_rejects   : {}{}",
    telemetry.duplicate_reject_count,
    expected_zero_marker(telemetry.duplicate_reject_count));
  LOG_F(INFO, "nexus.reclaimed_count     : {}{}", telemetry.reclaimed_count,
    expected_zero_marker(telemetry.reclaimed_count));
  LOG_F(INFO, "nexus.pending_count       : {}{}", telemetry.pending_count,
    expected_zero_marker(telemetry.pending_count));
  LOG_F(INFO, "peak transform slots      : {}", transforms_.size());
}

auto TransformUploader::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  slot_reuse_.OnBeginFrame(slot);
  ++frames_started_count_;
  frame_write_count_ = 0U;

  worlds_buffer_.OnFrameStart(sequence, slot);
  normals_buffer_.OnFrameStart(sequence, slot);

  // Clear cached SRV indices; they will be renewed during
  // EnsureFrameResources()
  worlds_srv_index_ = kInvalidShaderVisibleIndex;
  normals_srv_index_ = kInvalidShaderVisibleIndex;

  uploaded_this_frame_ = false;
}

auto TransformUploader::GetOrAllocate(const glm::mat4& transform)
  -> vortex::sceneprep::TransformHandle
{
  DCHECK_F(transforms::IsFinite(transform),
    "GetOrAllocate received non-finite matrix");

  // Strategy A remains deterministic here because allocation order is driven by
  // frame_write_count_ and reset at each OnFrameStart.
  const auto versioned_handle = slot_reuse_.Allocate(kTransformDomain);
  const auto index = versioned_handle.ToBindlessHandle().get();

  if (index >= transforms_.size()) {
    transforms_.push_back(transform);
    normal_matrices_.push_back(ComputeNormalMatrix(transform));
  } else {
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    transforms_[index] = transform;
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    normal_matrices_[index] = ComputeNormalMatrix(transform);
  }

  ++frame_write_count_;
  const auto handle = vortex::sceneprep::TransformHandle {
    vortex::sceneprep::TransformHandle::Index { index },
    vortex::sceneprep::TransformHandle::Generation {
      versioned_handle.GenerationValue() },
  };
  return handle;
}

auto TransformUploader::IsHandleValid(
  vortex::sceneprep::TransformHandle handle) const -> bool
{
  if (!handle.IsValid()) {
    return false;
  }
  const auto idx = handle.get();
  if (idx >= transforms_.size()) {
    return false;
  }

  return slot_reuse_.IsHandleCurrent(oxygen::VersionedBindlessHandle {
    bindless::HeapIndex { handle.get() },
    handle.GenerationValue(),
  });
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
  const auto& w_alloc = *w_res;

  auto n_res = normals_buffer_.Allocate(count);
  if (!n_res) {
    LOG_F(ERROR, "Failed to allocate normals transient buffer: {}",
      n_res.error().message());
    return;
  }
  const auto& n_alloc = *n_res;

  DLOG_F(1, "TransformUploader writing {} transforms to {}", count,
    fmt::ptr(w_alloc.mapped_ptr));

  if (!w_alloc.TryWriteRange(std::span { transforms_ })) {
    LOG_F(ERROR, "Failed to write world transforms into transient buffer");
    return;
  }
  if (!n_alloc.TryWriteRange(std::span { normal_matrices_ })) {
    LOG_F(ERROR, "Failed to write normal matrices into transient buffer");
    return;
  }

  // Cache SRV indices for GetWorldsSrvIndex() / GetNormalsSrvIndex()
  worlds_srv_index_ = w_alloc.srv;
  normals_srv_index_ = n_alloc.srv;

  uploaded_this_frame_ = true;
}

auto TransformUploader::GetWorldsSrvIndex() const -> ShaderVisibleIndex
{
  if (worlds_srv_index_ == kInvalidShaderVisibleIndex) {
    // Trigger lazy upload if not yet done
    // NOLINTNEXTLINE(*-pro-type-const-cast) - EnsureFrameResources is not const
    const_cast<TransformUploader*>(this)->EnsureFrameResources();
  }
  return worlds_srv_index_;
}

auto TransformUploader::GetNormalsSrvIndex() const -> ShaderVisibleIndex
{
  if (normals_srv_index_ == kInvalidShaderVisibleIndex) {
    // Trigger lazy upload if not yet done
    // NOLINTNEXTLINE(*-pro-type-const-cast) - EnsureFrameResources is not const
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
  const glm::mat3 upper_3x3 { world };
  const float det = glm::determinant(upper_3x3);

  constexpr float kDetEps = 1e-12F;
  if (!std::isfinite(det) || std::fabs(det) <= kDetEps) {
    return glm::mat4 { 1.0F };
  }

  const glm::mat3 normal_3x3 = glm::transpose(glm::inverse(upper_3x3));

  glm::mat4 result { 1.0F };
  result[0] = glm::vec4 { normal_3x3[0], 0.0F };
  result[1] = glm::vec4 { normal_3x3[1], 0.0F };
  result[2] = glm::vec4 { normal_3x3[2], 0.0F };
  return result;
}

} // namespace oxygen::vortex::resources
