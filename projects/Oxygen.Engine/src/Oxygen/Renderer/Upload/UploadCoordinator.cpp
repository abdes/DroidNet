//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <cstring>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Upload/StagingAllocator.h>
#include <Oxygen/Renderer/Upload/UploadPlanner.h>
// Format helpers for bytes-per-block and block size
#include <Oxygen/Core/Detail/FormatUtils.h>

namespace {

using namespace oxygen::graphics;
using namespace oxygen::engine::upload;

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
// Defers unmapping (if mapped) then releasing a staging buffer.
auto DeferUnmapThenRelease(
  detail::DeferredReclaimer& reclaimer, std::shared_ptr<Buffer>& buffer) -> void
{
  if (!buffer) {
    return;
  }
  reclaimer.RegisterDeferredAction([buf = std::move(buffer)]() mutable {
    if (buf) {
      if (buf->IsMapped()) {
        buf->UnMap();
      }
      buf.reset();
    }
  });
}

// Choose a queue key preferring the copy queue when available, otherwise
// falling back to the graphics queue.
// TODO: make this use the QueueSStrategy and part of the RenderConfig
auto ChooseUploadQueueKey(oxygen::Graphics& gfx) -> QueueKey
{
  const auto copy_key = SingleQueueStrategy().KeyFor(QueueRole::kTransfer);
  if (gfx.GetCommandQueue(copy_key) != nullptr) {
    return copy_key;
  }
  return SingleQueueStrategy().KeyFor(QueueRole::kGraphics);
}

// Minimal synchronous Submit: buffer uploads only.
// Follows Renderer.cpp pattern and uses SingleQueueStrategy for now.
auto SubmitBuffer(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  const auto& desc = std::get<UploadBufferDesc>(req.desc);
  const auto size = desc.size_bytes;
  if (!desc.dst || size == 0) {
    return {};
  }

  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { size }, req.debug_name);

  // Fill staging from the provided data view or producer
  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    auto to_copy = std::min<uint64_t>(size, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), to_copy);
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, size);
      if (!producer(dst)) {
        // Producer failed: return immediate failed ticket
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  // Record copy
  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitBuffer");
  recorder->BeginTrackingResourceState(
    *desc.dst, ResourceStates::kCommon, false);
  recorder->RequireResourceState(*desc.dst, ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBuffer(
    *desc.dst, desc.dst_offset, *staging.buffer, staging.offset, size);
  // Choose an appropriate steady-state based on declared buffer usage.
  const auto target_state = UsageToTargetState(desc.dst->GetUsage());
  recorder->RequireResourceState(*desc.dst, target_state);
  recorder->FlushBarriers();

  // Keep staging alive until execution; ensure unmap-before-release on retire
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  // Reserve a fence value on the target queue and record a GPU-side signal
  // into the command stream so completion is observed after the copy.
  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);

  return tracker.Register(FenceValue { fence_raw }, size, req.debug_name);
}

auto SubmitTexture2D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0) {
    return {};
  }

  // Use the planner to compute regions and total bytes.
  auto plan = UploadPlanner::PlanTexture2D(tdesc, req.subresources, policy);
  const uint64_t total_bytes = plan.total_bytes;

  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { total_bytes }, req.debug_name);

  // Fill staging
  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(total_bytes, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), to_copy);
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, total_bytes);
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  // Build upload region(s) from plan; adjust offsets by staging.offset.
  std::vector<TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  // Record copy to texture
  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture2D");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, ResourceStates::kCommon, false);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

auto SubmitTexture3D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0 || tdesc.depth == 0) {
    return {};
  }

  auto plan = UploadPlanner::PlanTexture3D(tdesc, req.subresources, policy);
  const uint64_t total_bytes = plan.total_bytes;
  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { total_bytes }, req.debug_name);

  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(total_bytes, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), to_copy);
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, total_bytes);
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  std::vector<TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture3D");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, ResourceStates::kCommon, false);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

auto SubmitTextureCube(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0) {
    return {};
  }

  auto plan = UploadPlanner::PlanTextureCube(tdesc, req.subresources, policy);
  const uint64_t total_bytes = plan.total_bytes;
  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { total_bytes }, req.debug_name);

  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(total_bytes, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), to_copy);
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, total_bytes);
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  std::vector<TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTextureCube");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, ResourceStates::kCommon, false);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

} // namespace

namespace oxygen::engine::upload {

UploadCoordinator::UploadCoordinator(Graphics& gfx, UploadPolicy policy)
  : gfx_(gfx)
  , policy_(policy)
{
}

auto UploadCoordinator::Submit(const UploadRequest& req) -> UploadTicket
{
  switch (req.kind) {
  case UploadKind::kBuffer:
    return SubmitBuffer(gfx_, req, policy_, tracker_);
  case UploadKind::kTexture2D:
    return SubmitTexture2D(gfx_, req, policy_, tracker_);
  case UploadKind::kTexture3D:
    return SubmitTexture3D(gfx_, req, policy_, tracker_);
  case UploadKind::kTextureCube:
    return SubmitTextureCube(gfx_, req, policy_, tracker_);
  }
  return {};
}

auto UploadCoordinator::SubmitMany(std::span<const UploadRequest> reqs)
  -> std::vector<UploadTicket>
{
  std::vector<UploadTicket> out;
  out.reserve(reqs.size());

  // For now: coalesce consecutive buffer requests when batch policy allows.
  // Otherwise, fall back to per-request Submit.
  size_t i = 0;
  while (i < reqs.size()) {
    // Gather a run of buffer requests marked Coalesce.
    size_t start = i;
    while (i < reqs.size() && reqs[i].kind == UploadKind::kBuffer
      && reqs[i].batch_policy == BatchPolicy::kCoalesce) {
      ++i;
    }
    const bool have_run = (i > start);
    if (!have_run) {
      out.emplace_back(Submit(reqs[i]));
      ++i;
      continue;
    }

    const auto span
      = std::span<const UploadRequest>(reqs.data() + start, i - start);
    auto plan = UploadPlanner::PlanBuffers(span, policy_);
    if (plan.total_bytes == 0 || plan.buffer_regions.empty()) {
      // No valid buffers in the run; submit individually for error accounting.
      for (size_t j = start; j < i; ++j) {
        out.emplace_back(Submit(reqs[j]));
      }
      continue;
    }

    StagingAllocator allocator(gfx_.shared_from_this());
    auto staging = allocator.Allocate(
      Bytes { plan.total_bytes }, "UploadCoordinator.BufferBatch");

    // Fill staging: concatenate views/producers; track failures per request.
    std::vector<bool> ok(span.size(), true);
    for (const auto& br : plan.buffer_regions) {
      const auto& r = span[br.request_index - start];
      if (std::holds_alternative<UploadDataView>(r.data)) {
        auto view = std::get<UploadDataView>(r.data);
        const auto to_copy = std::min<uint64_t>(br.size, view.bytes.size());
        if (to_copy > 0) {
          std::memcpy(staging.ptr + br.src_offset, view.bytes.data(), to_copy);
        }
      } else {
        auto& producer
          = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
            std::get<std::move_only_function<bool(std::span<std::byte>)>>(
              r.data));
        if (producer) {
          std::span<std::byte> dst(staging.ptr + br.src_offset, br.size);
          if (!producer(dst)) {
            ok[br.request_index - start] = false;
          }
        }
      }
    }

    const auto key = ChooseUploadQueueKey(gfx_);
    auto recorder = gfx_.AcquireCommandRecorder(
      key, "UploadCoordinator.SubmitBuffersBatch");

    // Record all copies and state transitions.
    for (const auto& br : plan.buffer_regions) {
      if (!ok[br.request_index - start]) {
        continue; // skip failed
      }
      recorder->BeginTrackingResourceState(
        *br.dst, ResourceStates::kCommon, false);
      recorder->RequireResourceState(*br.dst, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*br.dst, br.dst_offset, *staging.buffer,
        staging.offset + br.src_offset, br.size);
      // Choose steady-state based on declared buffer usage
      recorder->RequireResourceState(
        *br.dst, UsageToTargetState(br.dst->GetUsage()));
      recorder->FlushBarriers();
    }

    // Defer unmap-then-release for the batched staging buffer
    DeferUnmapThenRelease(gfx_.GetDeferredReclaimer(), staging.buffer);
    auto queue = gfx_.GetCommandQueue(key);
    const auto fence_raw = queue->Signal();
    recorder->RecordQueueSignal(fence_raw);

    // Create tickets for each request in the run (same fence, per-size bytes)
    for (const auto& br : plan.buffer_regions) {
      const auto& r = span[br.request_index - start];
      if (!ok[br.request_index - start]) {
        out.emplace_back(tracker_.RegisterFailedImmediate(r.debug_name,
          UploadError::kProducerFailed, "Producer returned false"));
      } else {
        out.emplace_back(
          tracker_.Register(FenceValue { fence_raw }, br.size, r.debug_name));
      }
    }
  }
  return out;
}

auto UploadCoordinator::RetireCompleted() -> void
{
  // Poll the same queue role used for uploads (transfer preferred)
  const auto key = ChooseUploadQueueKey(gfx_);
  if (auto q = gfx_.GetCommandQueue(key); q) {
    // Headless queues execute asynchronously on a worker thread when
    // SubmitDeferredCommandLists() is called. Ensure all pending submissions
    // for the chosen queue have been consumed so the GPU-side (executor)
    // QueueSignalCommand has a chance to advance the fence before we sample
    // it. This mirrors the pattern used in Headless_Smoke_test
    // (Queue::Wait/Flush).
    q->Flush();
    const auto completed = FenceValue { q->GetCompletedValue() };
    tracker_.MarkFenceCompleted(completed);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto UploadCoordinator::Flush() -> void { gfx_.SubmitDeferredCommandLists(); }

} // namespace oxygen::engine::upload
