//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <expected>
#include <functional>
#include <span>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Upload/RingBufferStaging.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

// Implementation of UploaderTagFactory. Provides access to UploaderTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal
#endif

namespace {

using namespace oxygen::graphics;
using namespace oxygen::engine::upload;

//! Determines if the given queue is a copy/transfer queue that has limited
//! resource state capabilities.
auto IsCopyQueue(const oxygen::observer_ptr<CommandQueue>& queue) -> bool
{
  return queue && queue->GetQueueRole() == QueueRole::kTransfer;
}

auto CanImplicitlyPromoteFromCommon(const ResourceStates initial_state) -> bool
{
  // In this engine, many textures default to kUndefined, which maps to
  // D3D12_RESOURCE_STATE_COMMON at creation time. For copy/transfer queues,
  // D3D12 expects resources to begin in COMMON and relies on implicit
  // promotion/decay rules.
  return initial_state == ResourceStates::kUnknown
    || initial_state == ResourceStates::kUndefined
    || initial_state == ResourceStates::kCommon;
}

// Maps a BufferUsage bitset to the preferred steady ResourceStates for
// buffers after upload. Defaults to kCommon when no specific usage is set.
auto UsageToTargetState(BufferUsage usage) -> ResourceStates
{
  if ((usage & BufferUsage::kIndex) == BufferUsage::kIndex) {
    return ResourceStates::kIndexBuffer;
  }
  if ((usage & BufferUsage::kVertex) == BufferUsage::kVertex) {
    return ResourceStates::kVertexBuffer;
  }
  if ((usage & BufferUsage::kConstant) == BufferUsage::kConstant) {
    return ResourceStates::kConstantBuffer;
  }
  if ((usage & BufferUsage::kStorage) == BufferUsage::kStorage) {
    // StructuredBuffer SRV steady state
    return ResourceStates::kShaderResource;
  }
  return ResourceStates::kCommon;
}

inline auto BlocksX(const oxygen::graphics::detail::FormatInfo& info,
  const uint32_t width_texels) -> uint32_t
{
  return (width_texels + info.block_size - 1U) / info.block_size;
}

inline auto BlocksY(const oxygen::graphics::detail::FormatInfo& info,
  const uint32_t height_texels) -> uint32_t
{
  return (height_texels + info.block_size - 1U) / info.block_size;
}

inline auto RowCopyBytes(const oxygen::graphics::detail::FormatInfo& info,
  const uint32_t width_texels) -> uint64_t
{
  return static_cast<uint64_t>(BlocksX(info, width_texels))
    * static_cast<uint64_t>(info.bytes_per_block);
}

auto PackTexture2DToStaging(const UploadPolicy& policy,
  const oxygen::graphics::detail::FormatInfo& info, const TextureUploadPlan& plan,
  const UploadTextureSourceView& src, std::byte* dst_staging) -> bool
{
  const auto& fp = policy.filler;
  if (fp.enable_default_fill) {
    std::memset(dst_staging, static_cast<int>(fp.filler_value),
      plan.total_bytes);
  }

  if (plan.regions.size() != plan.source_indices.size()) {
    return false;
  }

  for (std::size_t i = 0; i < plan.regions.size(); ++i) {
    const auto& region = plan.regions[i];
    const auto src_index = plan.source_indices[i];
    if (src_index >= src.subresources.size()) {
      return false;
    }
    const auto& s = src.subresources[src_index];
    if (s.row_pitch == 0U || s.slice_pitch == 0U) {
      return false;
    }

    const auto width = region.dst_slice.width;
    const auto height = region.dst_slice.height;
    const auto copy_bytes_per_row = RowCopyBytes(info, width);
    const auto rows = BlocksY(info, height);

    const uint64_t required_src_bytes =
      (rows == 0U) ? 0ULL
                  : (static_cast<uint64_t>(rows - 1U)
                      * static_cast<uint64_t>(s.row_pitch))
        + copy_bytes_per_row;
    if (s.bytes.size() < required_src_bytes) {
      return false;
    }
    if (s.row_pitch < copy_bytes_per_row) {
      return false;
    }

    const uint64_t required_dst_bytes =
      (rows == 0U) ? 0ULL
                  : (static_cast<uint64_t>(rows - 1U)
                      * static_cast<uint64_t>(region.buffer_row_pitch))
        + copy_bytes_per_row;
    if (region.buffer_offset + required_dst_bytes > plan.total_bytes) {
      return false;
    }

    for (uint32_t row = 0; row < rows; ++row) {
      const auto src_off = static_cast<uint64_t>(row)
        * static_cast<uint64_t>(s.row_pitch);
      const auto dst_off = region.buffer_offset
        + static_cast<uint64_t>(row)
          * static_cast<uint64_t>(region.buffer_row_pitch);
      std::memcpy(dst_staging + dst_off,
        s.bytes.data() + static_cast<std::size_t>(src_off),
        static_cast<std::size_t>(copy_bytes_per_row));
    }
  }

  return true;
}

auto PackTexture3DToStaging(const UploadPolicy& policy,
  const oxygen::graphics::detail::FormatInfo& info, const TextureUploadPlan& plan,
  const UploadTextureSourceView& src, std::byte* dst_staging) -> bool
{
  const auto& fp = policy.filler;
  if (fp.enable_default_fill) {
    std::memset(dst_staging, static_cast<int>(fp.filler_value),
      plan.total_bytes);
  }

  if (plan.regions.size() != plan.source_indices.size()) {
    return false;
  }

  for (std::size_t i = 0; i < plan.regions.size(); ++i) {
    const auto& region = plan.regions[i];
    const auto src_index = plan.source_indices[i];
    if (src_index >= src.subresources.size()) {
      return false;
    }
    const auto& s = src.subresources[src_index];
    if (s.row_pitch == 0U || s.slice_pitch == 0U) {
      return false;
    }

    const auto width = region.dst_slice.width;
    const auto height = region.dst_slice.height;
    const auto depth = region.dst_slice.depth;
    const auto copy_bytes_per_row = RowCopyBytes(info, width);
    const auto rows = BlocksY(info, height);

    if (s.row_pitch < copy_bytes_per_row) {
      return false;
    }

    const uint64_t required_src_bytes =
      (depth == 0U || rows == 0U)
        ? 0ULL
        : (static_cast<uint64_t>(depth - 1U)
            * static_cast<uint64_t>(s.slice_pitch))
          + (static_cast<uint64_t>(rows - 1U)
              * static_cast<uint64_t>(s.row_pitch))
          + copy_bytes_per_row;
    if (s.bytes.size() < required_src_bytes) {
      return false;
    }

    const uint64_t required_dst_bytes =
      (depth == 0U || rows == 0U)
        ? 0ULL
        : (static_cast<uint64_t>(depth - 1U)
            * static_cast<uint64_t>(region.buffer_slice_pitch))
          + (static_cast<uint64_t>(rows - 1U)
              * static_cast<uint64_t>(region.buffer_row_pitch))
          + copy_bytes_per_row;
    if (region.buffer_offset + required_dst_bytes > plan.total_bytes) {
      return false;
    }

    for (uint32_t z = 0; z < depth; ++z) {
      const auto src_slice_off =
        static_cast<uint64_t>(z) * static_cast<uint64_t>(s.slice_pitch);
      const auto dst_slice_off = region.buffer_offset
        + static_cast<uint64_t>(z)
          * static_cast<uint64_t>(region.buffer_slice_pitch);
      for (uint32_t row = 0; row < rows; ++row) {
        const auto src_off = src_slice_off + static_cast<uint64_t>(row)
          * static_cast<uint64_t>(s.row_pitch);
        const auto dst_off = dst_slice_off + static_cast<uint64_t>(row)
          * static_cast<uint64_t>(region.buffer_row_pitch);
        std::memcpy(dst_staging + dst_off,
          s.bytes.data() + static_cast<std::size_t>(src_off),
          static_cast<std::size_t>(copy_bytes_per_row));
      }
    }
  }

  return true;
}

// Minimal synchronous Submit: buffer uploads only.
// Follows Renderer.cpp pattern and uses SingleQueueStrategy for now.
auto SubmitBuffer(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker, StagingProvider& provider,
  const QueueKey& queue_key)
  -> std::expected<UploadTicket, UploadError>
{
  const auto& desc = std::get<UploadBufferDesc>(req.desc);
  const auto size = desc.size_bytes;
  if (!desc.dst || size == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Allocate staging directly from the provider
  auto staging_exp = provider.Allocate(SizeBytes { size }, req.debug_name);
  if (!staging_exp) {
    return std::unexpected(staging_exp.error());
  }
  auto staging = std::move(*staging_exp);

  // Fill staging from the provided data view or producer.
  const auto& fp = policy.filler;
  if (fp.enable_default_fill) {
    std::memset(
      staging.Ptr(), static_cast<int>(fp.filler_value), static_cast<size_t>(size));
  }

  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(size, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.Ptr(), view.bytes.data(), static_cast<size_t>(to_copy));
    }
  } else if (std::holds_alternative<UploadProducer>(req.data)) {
    auto& producer
      = const_cast<UploadProducer&>(std::get<UploadProducer>(req.data));
    if (!producer) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    std::span<std::byte> dst(staging.Ptr(), size);
    if (!producer(dst)) {
      return tracker.RegisterFailedImmediate(
        req.debug_name, UploadError::kProducerFailed);
    }
  } else {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Record copy
  const auto& key = queue_key;
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitBuffer");
  auto queue = gfx.GetCommandQueue(key);
  const bool is_copy_queue = IsCopyQueue(queue);

  // Begin tracking with appropriate parameters for the queue type.
  // Copy queues: start from kCommon and restore to kCommon when done.
  // Graphics queues: start from kCopyDest and don't restore initial state.
  const auto initial_state
    = is_copy_queue ? ResourceStates::kCommon : ResourceStates::kCopyDest;
  const bool keep_initial_state = is_copy_queue;

  recorder->BeginTrackingResourceState(
    *desc.dst, initial_state, keep_initial_state);
  recorder->RequireResourceState(*desc.dst, ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBuffer(
    *desc.dst, desc.dst_offset, staging.Buffer(), staging.Offset().get(), size);

  // For copy queues, let the resource state tracker automatically restore to
  // kCommon (because keep_initial_state = true). For graphics queues,
  // transition to the appropriate usage-specific state.
  if (!is_copy_queue) {
    const auto target_state = UsageToTargetState(desc.dst->GetUsage());
    recorder->RequireResourceState(*desc.dst, target_state);
    recorder->FlushBarriers();
  }

  // Reserve a fence value on the target queue and record a GPU-side signal
  // into the command stream so completion is observed after the copy.
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);

  return tracker.Register(FenceValue { fence_raw }, size, req.debug_name);
}

auto SubmitTexture2D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker, StagingProvider& provider)
  -> std::expected<UploadTicket, UploadError>
{
  // Use the planner to compute regions and total bytes.
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  auto exp_plan = UploadPlanner::PlanTexture2D(tdesc, req.subresources, policy);
  if (!exp_plan.has_value()) {
    return std::unexpected(exp_plan.error());
  }
  const auto plan = exp_plan.value();
  const uint64_t total_bytes = plan.total_bytes;

  auto staging_exp
    = provider.Allocate(SizeBytes { total_bytes }, req.debug_name);
  if (!staging_exp) {
    return std::unexpected(staging_exp.error());
  }
  auto staging = std::move(*staging_exp);

  const auto info = oxygen::graphics::detail::GetFormatInfo(
    tdesc.dst->GetDescriptor().format);
  if (info.bytes_per_block == 0 || info.block_size == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Fill staging.
  const auto& fp = policy.filler;
  if (fp.enable_default_fill) {
    std::memset(staging.Ptr(), static_cast<int>(fp.filler_value), total_bytes);
  }

  if (std::holds_alternative<UploadTextureSourceView>(req.data)) {
    const auto& src_view = std::get<UploadTextureSourceView>(req.data);
    if (!PackTexture2DToStaging(policy, info, plan, src_view,
          reinterpret_cast<std::byte*>(staging.Ptr()))) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
  } else if (std::holds_alternative<UploadProducer>(req.data)) {
    auto& producer
      = const_cast<UploadProducer&>(std::get<UploadProducer>(req.data));
    if (!producer) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    std::span<std::byte> dst(
      reinterpret_cast<std::byte*>(staging.Ptr()), total_bytes);
    if (!producer(dst)) {
      return tracker.RegisterFailedImmediate(
        req.debug_name, UploadError::kProducerFailed);
    }
  } else {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  // Build upload region(s) from plan; adjust offsets by staging.offset.
  std::vector<TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.Offset().get();
  }

  // Record copy to texture
  const auto& key = policy.upload_queue_key;
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture2D");
  auto queue = gfx.GetCommandQueue(key);
  const bool is_copy_queue = IsCopyQueue(queue);

  // Track the destination texture using its declared initial state when
  // available. This avoids issuing barriers with an incorrect StateBefore.
  const auto dst_initial_state = tdesc.dst->GetDescriptor().initial_state;
  const auto tracking_initial_state
    = (dst_initial_state == ResourceStates::kUnknown
        || dst_initial_state == ResourceStates::kUndefined)
    ? ResourceStates::kCommon
    : dst_initial_state;

  // On copy/transfer queues, resources in COMMON can be implicitly promoted
  // for copy operations and decay back to COMMON after execution. Avoiding
  // explicit COPY_DEST transitions here prevents DX12 debug-layer complaints
  // when the runtime relies on implicit promotion for CopyTextureRegion.
  const bool use_implicit_promotion
    = is_copy_queue && CanImplicitlyPromoteFromCommon(tracking_initial_state);

  // For copy queues, keep_initial_state=true restores the tracked state when
  // the command list closes. For graphics queues, manage transitions
  // explicitly.
  recorder->BeginTrackingResourceState(
    *tdesc.dst, tracking_initial_state, is_copy_queue);
  if (!use_implicit_promotion) {
    recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
  }
  recorder->CopyBufferToTexture(
    staging.Buffer(), std::span { regions }, *tdesc.dst);

  // For copy queues, the resource tracker will auto-restore to kCommon.
  // For graphics queues, explicitly transition back to kCommon.
  if (!is_copy_queue) {
    recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCommon);
    recorder->FlushBarriers();
  }

  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

auto SubmitTexture3D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker, StagingProvider& provider)
  -> std::expected<UploadTicket, UploadError>
{
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  auto exp_plan = UploadPlanner::PlanTexture3D(tdesc, req.subresources, policy);
  if (!exp_plan.has_value()) {
    return std::unexpected(exp_plan.error());
  }
  const auto plan = exp_plan.value();
  const uint64_t total_bytes = plan.total_bytes;
  auto staging_exp
    = provider.Allocate(SizeBytes { total_bytes }, req.debug_name);
  if (!staging_exp) {
    return std::unexpected(staging_exp.error());
  }
  auto staging = std::move(*staging_exp);

  const auto info = oxygen::graphics::detail::GetFormatInfo(
    tdesc.dst->GetDescriptor().format);
  if (info.bytes_per_block == 0 || info.block_size == 0) {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  const auto& fp = policy.filler;
  if (fp.enable_default_fill) {
    std::memset(staging.Ptr(), static_cast<int>(fp.filler_value), total_bytes);
  }

  if (std::holds_alternative<UploadTextureSourceView>(req.data)) {
    const auto& src_view = std::get<UploadTextureSourceView>(req.data);
    if (!PackTexture3DToStaging(policy, info, plan, src_view,
          reinterpret_cast<std::byte*>(staging.Ptr()))) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
  } else if (std::holds_alternative<UploadProducer>(req.data)) {
    auto& producer
      = const_cast<UploadProducer&>(std::get<UploadProducer>(req.data));
    if (!producer) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    std::span<std::byte> dst(
      reinterpret_cast<std::byte*>(staging.Ptr()), total_bytes);
    if (!producer(dst)) {
      return tracker.RegisterFailedImmediate(
        req.debug_name, UploadError::kProducerFailed);
    }
  } else {
    return std::unexpected(UploadError::kInvalidRequest);
  }

  std::vector<TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.Offset().get();
  }

  const auto& key = policy.upload_queue_key;
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture3D");
  auto queue = gfx.GetCommandQueue(key);
  const bool is_copy_queue = IsCopyQueue(queue);

  const auto dst_initial_state = tdesc.dst->GetDescriptor().initial_state;
  const auto tracking_initial_state
    = (dst_initial_state == ResourceStates::kUnknown
        || dst_initial_state == ResourceStates::kUndefined)
    ? ResourceStates::kCommon
    : dst_initial_state;
  const bool use_implicit_promotion
    = is_copy_queue && CanImplicitlyPromoteFromCommon(tracking_initial_state);

  recorder->BeginTrackingResourceState(
    *tdesc.dst, tracking_initial_state, is_copy_queue);
  if (!use_implicit_promotion) {
    recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
  }
  recorder->CopyBufferToTexture(
    staging.Buffer(), std::span { regions }, *tdesc.dst);

  // For copy queues, the resource tracker will auto-restore to kCommon.
  // For graphics queues, explicitly transition back to kCommon.
  if (!is_copy_queue) {
    recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCommon);
    recorder->FlushBarriers();
  }

  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

} // namespace

namespace oxygen::engine::upload {

UploadCoordinator::UploadCoordinator(
  observer_ptr<Graphics> gfx, UploadPolicy policy)
  : gfx_(gfx)
  , policy_(policy)
{
  DCHECK_NOTNULL_F(gfx_);
}

auto UploadCoordinator::CreateRingBufferStaging(frame::SlotCount partitions,
  std::uint32_t alignment, float slack) -> std::shared_ptr<StagingProvider>
{
  auto provider = std::make_shared<RingBufferStaging>(
    internal::UploaderTagFactory::Get(), gfx_, partitions, alignment, slack);
  providers_.push_back(provider);
  return provider;
}

auto UploadCoordinator::Submit(const UploadRequest& req,
  StagingProvider& provider) -> std::expected<UploadTicket, UploadError>
{
  if (shutting_down_.load()) {
    return std::unexpected(UploadError::kTrackerShutdown);
  }
  switch (req.kind) {
  case UploadKind::kBuffer:
    return SubmitBuffer(
      *gfx_, req, policy_, tracker_, provider, policy_.upload_queue_key);
  case UploadKind::kTexture2D:
    return SubmitTexture2D(*gfx_, req, policy_, tracker_, provider);
  case UploadKind::kTexture3D:
    return SubmitTexture3D(*gfx_, req, policy_, tracker_, provider);
  }
  return std::unexpected(UploadError::kInvalidRequest);
}

auto UploadCoordinator::SubmitMany(
  std::span<const UploadRequest> reqs, StagingProvider& provider)
  -> std::expected<std::vector<UploadTicket>, UploadError>
{
  if (shutting_down_.load()) {
    return std::unexpected(UploadError::kTrackerShutdown);
  }
  std::vector<UploadTicket> out;
  out.reserve(reqs.size());

  // Coalesce consecutive buffer requests. Non-buffer requests are submitted
  // individually. All coalescing/optimization is handled by UploadPlanner.
  size_t idx = 0;
  while (idx < reqs.size()) {
    // Gather a run of consecutive buffer requests.
    size_t start = idx;
    while (idx < reqs.size() && reqs[idx].kind == UploadKind::kBuffer) {
      ++idx;
    }
    if (idx == start) {
      auto submit_result = Submit(reqs[idx], provider);
      if (!submit_result) {
        return std::unexpected(submit_result.error());
      }
      out.emplace_back(std::move(*submit_result));
      ++idx;
      continue;
    }
    auto exp_tickets
      = SubmitRun({ reqs.data() + start, idx - start }, provider);
    if (!exp_tickets) {
      return std::unexpected(exp_tickets.error());
    }

    out.insert(out.end(), std::make_move_iterator(exp_tickets->begin()),
      std::make_move_iterator(exp_tickets->end()));
  }
  return out;
}

auto UploadCoordinator::Shutdown(std::chrono::milliseconds timeout)
  -> std::expected<void, UploadError>
{
  shutting_down_.store(true);

  using namespace std::chrono_literals;
  const auto start = std::chrono::steady_clock::now();
  std::chrono::milliseconds backoff { 1 };

  // Capture the highest fence value registered so far. This ensures that we
  // will wait for any recorded submissions even if individual ticket
  // entries are later erased by frame-slot cleanup.
  const auto target_fence = tracker_.LastRegisteredFence();

  // Loop and politely poll the queue, using exponential backoff to avoid a
  // hot spin. We primarily wait until either there are no tracked pending
  // entries or the completed fence advances to the last observed fence.
  while (tracker_.HasPending() || tracker_.CompletedFence() < target_fence) {
    RetireCompleted();

    if (!(tracker_.HasPending() || tracker_.CompletedFence() < target_fence)) {
      break; // work finished after retire
    }

    if (std::chrono::steady_clock::now() - start > timeout) {
      LOG_F(WARNING, "UploadCoordinator::Shutdown timed out after {}ms",
        timeout.count());
      return std::unexpected(UploadError::kSubmitFailed);
    }

    std::this_thread::sleep_for(backoff);
    // Exponential backoff with a small ceiling to remain responsive.
    backoff = (std::min)(backoff * 2, std::chrono::milliseconds { 50 });
  }

  // One final retire so providers can recycle any completed allocations.
  RetireCompleted();

  return {};
}

auto UploadCoordinator::RetireCompleted() -> void
{
  // Poll the upload queue configured in the policy.
  const auto& key = policy_.upload_queue_key;
  if (auto q = gfx_->GetCommandQueue(key); q) {
    q->Flush();
    const auto completed = FenceValue { q->GetCompletedValue() };
    tracker_.MarkFenceCompleted(completed);
    // Allow providers to recycle now that fence advanced
    auto tag = internal::UploaderTagFactory::Get();
    for (auto it = providers_.begin(); it != providers_.end();) {
      if (auto sp = it->lock()) {
        sp->RetireCompleted(tag, completed);
        ++it;
      } else {
        it = providers_.erase(it);
      }
    }
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto UploadCoordinator::OnFrameStart(renderer::RendererTag, frame::Slot slot)
  -> void
{
  static auto tag = internal::UploaderTagFactory::Get();

  RetireCompleted();

  tracker_.OnFrameStart(tag, slot);

  for (auto it = providers_.begin(); it != providers_.end();) {
    if (auto sp = it->lock()) {
      sp->OnFrameStart(tag, slot);
      ++it;
    } else {
      it = providers_.erase(it);
    }
  }
}

auto UploadCoordinator::SubmitAsync(
  const UploadRequest& req, StagingProvider& provider) -> co::Co<UploadResult>
{
  auto submit_result = Submit(req, provider);
  if (!submit_result.has_value()) {
    co_return UploadResult {
      .success = false,
      .error = submit_result.error(),
    };
  }
  auto t = submit_result.value();
  co_await AwaitAsync(t);
  auto result = TryGetResult(t);
  DCHECK_F(result.has_value(),
    "Ticket result must be available after successful await");
  co_return result.value();
}

auto UploadCoordinator::SubmitManyAsync(std::span<const UploadRequest> reqs,
  StagingProvider& provider) -> co::Co<std::vector<UploadResult>>
{
  auto submit_result = SubmitMany(reqs, provider);
  if (!submit_result.has_value()) {
    co_return std::vector<UploadResult> {
      UploadResult {
        .success = false,
        .error = submit_result.error(),
      },
    };
  }
  auto tickets = submit_result.value();
  co_await AwaitAllAsync(tickets);
  std::vector<UploadResult> out;
  out.reserve(tickets.size());
  for (auto t : tickets) {
    auto result = TryGetResult(t);
    DCHECK_F(result.has_value(),
      "Ticket result must be available after successful AwaitAll");
    out.emplace_back(result.value());
  }
  co_return out;
}

auto UploadCoordinator::AwaitAsync(UploadTicket t) -> co::Co<void>
{
  co_await Until(tracker_.CompletedFenceValue() >= t.fence);
  co_return; // result can be queried if needed
}

auto UploadCoordinator::AwaitAllAsync(std::span<const UploadTicket> tickets)
  -> co::Co<void>
{
  if (tickets.empty()) {
    co_return;
  }
  FenceValue max_fence { 0 };
  for (const auto& t : tickets) {
    if (max_fence < t.fence) {
      max_fence = t.fence;
    }
  }
  co_await Until(tracker_.CompletedFenceValue() >= max_fence);
  co_return;
}

//=== SubmitMany decomposition helpers ------------------------------------//

auto UploadCoordinator::SubmitRun(
  std::span<const UploadRequest> run, StagingProvider& provider)
  -> std::expected<std::vector<UploadTicket>, UploadError>
{
  // Plan → FillStaging → Optimize → Record → Tickets
  auto plan_result = PlanBufferRun(run);
  if (!plan_result) {
    return std::unexpected(plan_result.error());
  }
  auto& plan = plan_result.value();
  auto allocation_result
    = provider.Allocate(SizeBytes { plan.total_bytes }, "BatchUpload");
  if (!allocation_result) {
    return std::unexpected(allocation_result.error());
  }
  auto& allocation = *allocation_result;
  FillStagingForPlan(plan, run, allocation);
  return OptimizeBufferRun(run, plan).and_then([&](BufferUploadPlan opt) {
    return RecordBufferRun(opt, run, allocation)
      .and_then([&](graphics::FenceValue fence) {
        return MakeTicketsForPlan(plan, run, fence);
      });
  });
}

auto UploadCoordinator::PlanBufferRun(std::span<const UploadRequest> run)
  -> std::expected<BufferUploadPlan, UploadError>
{
  return UploadPlanner::PlanBuffers(run, policy_);
}

auto UploadCoordinator::FillStagingForPlan(const BufferUploadPlan& plan,
  std::span<const UploadRequest> run, StagingProvider::Allocation& allocation)
  -> void
{
  const auto& fp = policy_.filler;
  for (const auto& it : plan.uploads) {
    const auto rep = it.request_indices.front();
    const auto& r = run[rep];
    const auto& reg = it.region;
    if (std::holds_alternative<UploadDataView>(r.data)) {
      auto view = std::get<UploadDataView>(r.data);
      const auto to_copy = std::min<uint64_t>(reg.size, view.bytes.size());
      if (to_copy > 0) {
        std::memcpy(
          allocation.Ptr() + reg.src_offset, view.bytes.data(), to_copy);
      }
      if (fp.enable_default_fill && to_copy < reg.size) {
        std::memset(allocation.Ptr() + reg.src_offset + to_copy,
          static_cast<int>(fp.filler_value), reg.size - to_copy);
      }
    } else {
      auto& producer
        = const_cast<UploadProducer&>(std::get<UploadProducer>(r.data));
      std::span<std::byte> dst(allocation.Ptr() + reg.src_offset, reg.size);
      if (!producer && fp.enable_default_fill) {
        std::memset(dst.data(), static_cast<int>(fp.filler_value), dst.size());
      } else if (producer) {
        if (!producer(dst) && fp.enable_default_fill) {
          std::memset(
            dst.data(), static_cast<int>(fp.filler_value), dst.size());
        }
      }
    }
  }
}

auto UploadCoordinator::OptimizeBufferRun(std::span<const UploadRequest> run,
  const BufferUploadPlan& plan) -> std::expected<BufferUploadPlan, UploadError>
{
  return UploadPlanner::OptimizeBuffers(run, plan, policy_);
}

auto UploadCoordinator::RecordBufferRun(const BufferUploadPlan& optimized,
  std::span<const UploadRequest> run, StagingProvider::Allocation& staging)
  -> std::expected<graphics::FenceValue, UploadError>
{
  const auto& key = policy_.upload_queue_key;
  auto recorder
    = gfx_->AcquireCommandRecorder(key, "UploadCoordinator.SubmitBuffersBatch");
  auto queue = gfx_->GetCommandQueue(key);
  const bool is_copy_queue = IsCopyQueue(queue);

  std::shared_ptr<Buffer> current_dst;
  for (size_t idx2 = 0; idx2 < optimized.uploads.size(); ++idx2) {
    const auto& it = optimized.uploads[idx2];
    if (it.request_indices.empty()) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    const auto rep = it.request_indices.front();
    const auto& r = run[rep];
    const auto& bdesc = std::get<UploadBufferDesc>(r.desc);
    auto dst = bdesc.dst;
    if (!dst) {
      return std::unexpected(UploadError::kInvalidRequest);
    }

    const bool first_for_dst
      = (!current_dst) || (current_dst.get() != dst.get());
    if (first_for_dst) {
      current_dst = dst;

      // Begin tracking with appropriate parameters for the queue type.
      // Copy queues: start from kCommon and restore to kCommon when done.
      // Graphics queues: start from kCommon and don't restore initial state.
      const auto initial_state = ResourceStates::kCommon;
      const bool keep_initial_state = is_copy_queue;

      recorder->BeginTrackingResourceState(
        *current_dst, initial_state, keep_initial_state);
      recorder->RequireResourceState(*current_dst, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
    }

    recorder->CopyBuffer(*dst, it.region.dst_offset, staging.Buffer(),
      staging.Offset().get() + it.region.src_offset, it.region.size);

    const bool is_last = (idx2 + 1 == optimized.uploads.size());
    const bool next_diff = !is_last && [&]() {
      const auto& next_it = optimized.uploads[idx2 + 1];
      const auto next_rep = next_it.request_indices.front();
      const auto& next_r = run[next_rep];
      const auto& next_bdesc = std::get<UploadBufferDesc>(next_r.desc);
      return next_bdesc.dst.get() != dst.get();
    }();

    // Handle final state transition only for graphics queues.
    // Copy queues will automatically restore to kCommon due to
    // keep_initial_state=true.
    if ((is_last || next_diff) && !is_copy_queue) {
      recorder->RequireResourceState(*dst, UsageToTargetState(dst->GetUsage()));
      recorder->FlushBarriers();
    }
  }

  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return graphics::FenceValue { fence_raw };
}

auto UploadCoordinator::MakeTicketsForPlan(
  const BufferUploadPlan& original_plan, std::span<const UploadRequest> run,
  graphics::FenceValue fence)
  -> std::expected<std::vector<UploadTicket>, UploadError>
{
  std::vector<UploadTicket> tickets;
  tickets.reserve(original_plan.uploads.size());
  for (const auto& it : original_plan.uploads) {
    if (it.request_indices.empty()) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    const auto rep = it.request_indices.front();
    const auto& r = run[rep];
    tickets.emplace_back(
      tracker_.Register(fence, it.region.size, r.debug_name));
  }
  return tickets;
}

} // namespace oxygen::engine::upload
