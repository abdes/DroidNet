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

namespace oxygen::engine::upload {

namespace {
  inline void EnsureQueues(oxygen::Graphics& gfx)
  {
    if (gfx.GetCommandQueue(graphics::QueueRole::kGraphics) == nullptr) {
      gfx.CreateCommandQueues(graphics::SingleQueueStrategy());
    }
  }
  // Maps a BufferUsage bitset to the preferred steady ResourceStates for
  // buffers after upload. Defaults to kCommon when no specific usage is set.
  inline auto UsageToTargetState(graphics::BufferUsage usage)
    -> graphics::ResourceStates
  {
    using graphics::BufferUsage;
    using graphics::ResourceStates;
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
  inline void DeferUnmapThenRelease(
    oxygen::graphics::detail::DeferredReclaimer& reclaimer,
    std::shared_ptr<oxygen::graphics::Buffer>& buffer)
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
  inline auto ChooseUploadQueueKey(oxygen::Graphics& gfx) -> graphics::QueueKey
  {
    const auto copy_key
      = graphics::SingleQueueStrategy().KeyFor(graphics::QueueRole::kTransfer);
    if (gfx.GetCommandQueue(copy_key) != nullptr) {
      return copy_key;
    }
    return graphics::SingleQueueStrategy().KeyFor(
      graphics::QueueRole::kGraphics);
  }
} // namespace

// Minimal synchronous Submit: buffer uploads only.
// Follows Renderer.cpp pattern and uses SingleQueueStrategy for now.
static auto SubmitBuffer(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  (void)policy;
  EnsureQueues(gfx);
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
      std::memcpy(staging.ptr, view.bytes.data(), static_cast<size_t>(to_copy));
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, static_cast<size_t>(size));
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
    *desc.dst, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(
    *desc.dst, graphics::ResourceStates::kCopyDest);
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

static auto SubmitTexture2D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  EnsureQueues(gfx);
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0)
    return {};

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
      std::memcpy(staging.ptr, view.bytes.data(), static_cast<size_t>(to_copy));
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, static_cast<size_t>(total_bytes));
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  // Build upload region(s) from plan; adjust offsets by staging.offset.
  std::vector<graphics::TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  // Record copy to texture
  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture2D");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(
    *tdesc.dst, graphics::ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, graphics::ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

static auto SubmitTexture3D(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  EnsureQueues(gfx);
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0 || tdesc.depth == 0)
    return {};

  auto plan = UploadPlanner::PlanTexture3D(tdesc, req.subresources, policy);
  const uint64_t total_bytes = plan.total_bytes;
  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { total_bytes }, req.debug_name);

  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(total_bytes, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), static_cast<size_t>(to_copy));
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, static_cast<size_t>(total_bytes));
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  std::vector<graphics::TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTexture3D");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(
    *tdesc.dst, graphics::ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, graphics::ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

static auto SubmitTextureCube(oxygen::Graphics& gfx, const UploadRequest& req,
  const UploadPolicy& policy, UploadTracker& tracker) -> UploadTicket
{
  EnsureQueues(gfx);
  const auto& tdesc = std::get<UploadTextureDesc>(req.desc);
  if (!tdesc.dst || tdesc.width == 0 || tdesc.height == 0)
    return {};

  auto plan = UploadPlanner::PlanTextureCube(tdesc, req.subresources, policy);
  const uint64_t total_bytes = plan.total_bytes;
  StagingAllocator allocator(gfx.shared_from_this());
  auto staging = allocator.Allocate(Bytes { total_bytes }, req.debug_name);

  if (std::holds_alternative<UploadDataView>(req.data)) {
    auto view = std::get<UploadDataView>(req.data);
    const auto to_copy = std::min<uint64_t>(total_bytes, view.bytes.size());
    if (to_copy > 0) {
      std::memcpy(staging.ptr, view.bytes.data(), static_cast<size_t>(to_copy));
    }
  } else {
    auto& producer
      = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
        std::get<std::move_only_function<bool(std::span<std::byte>)>>(
          req.data));
    if (producer) {
      std::span<std::byte> dst(staging.ptr, static_cast<size_t>(total_bytes));
      if (!producer(dst)) {
        return tracker.RegisterFailedImmediate(req.debug_name,
          UploadError::kProducerFailed, "Producer returned false");
      }
    }
  }

  std::vector<graphics::TextureUploadRegion> regions = plan.regions;
  for (auto& r : regions) {
    r.buffer_offset += staging.offset;
  }

  const auto key = ChooseUploadQueueKey(gfx);
  auto recorder
    = gfx.AcquireCommandRecorder(key, "UploadCoordinator.SubmitTextureCube");
  recorder->BeginTrackingResourceState(
    *tdesc.dst, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(
    *tdesc.dst, graphics::ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBufferToTexture(
    *staging.buffer, std::span { regions }, *tdesc.dst);
  recorder->RequireResourceState(*tdesc.dst, graphics::ResourceStates::kCommon);
  recorder->FlushBarriers();

  // Defer unmap-then-release of staging buffer once safe to reclaim
  DeferUnmapThenRelease(gfx.GetDeferredReclaimer(), staging.buffer);

  auto queue = gfx.GetCommandQueue(key);
  const auto fence_raw = queue->Signal();
  recorder->RecordQueueSignal(fence_raw);
  return tracker.Register(
    FenceValue { fence_raw }, total_bytes, req.debug_name);
}

auto UploadCoordinator::Submit(const UploadRequest& req) -> UploadTicket
{
  auto g = graphics_.get();
  if (!g)
    return {};
  switch (req.kind) {
  case UploadKind::kBuffer:
    return SubmitBuffer(*g, req, policy_, tracker_);
  case UploadKind::kTexture2D:
    return SubmitTexture2D(*g, req, policy_, tracker_);
  case UploadKind::kTexture3D:
    return SubmitTexture3D(*g, req, policy_, tracker_);
  case UploadKind::kTextureCube:
    return SubmitTextureCube(*g, req, policy_, tracker_);
  default:
    return {};
  }
}

auto UploadCoordinator::SubmitMany(std::span<const UploadRequest> reqs)
  -> std::vector<UploadTicket>
{
  std::vector<UploadTicket> out;
  out.reserve(reqs.size());
  auto g = graphics_.get();
  if (!g || reqs.empty())
    return out;

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

    EnsureQueues(*g);
    StagingAllocator allocator(g->shared_from_this());
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
          std::memcpy(staging.ptr + br.src_offset, view.bytes.data(),
            static_cast<size_t>(to_copy));
        }
      } else {
        auto& producer
          = const_cast<std::move_only_function<bool(std::span<std::byte>)>&>(
            std::get<std::move_only_function<bool(std::span<std::byte>)>>(
              r.data));
        if (producer) {
          std::span<std::byte> dst(
            staging.ptr + br.src_offset, static_cast<size_t>(br.size));
          if (!producer(dst)) {
            ok[br.request_index - start] = false;
          }
        }
      }
    }

    const auto key = ChooseUploadQueueKey(*g);
    auto recorder
      = g->AcquireCommandRecorder(key, "UploadCoordinator.SubmitBuffersBatch");

    // Record all copies and state transitions.
    for (const auto& br : plan.buffer_regions) {
      if (!ok[br.request_index - start])
        continue; // skip failed
      recorder->BeginTrackingResourceState(
        *br.dst, graphics::ResourceStates::kCommon, false);
      recorder->RequireResourceState(
        *br.dst, graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*br.dst, br.dst_offset, *staging.buffer,
        staging.offset + br.src_offset, br.size);
      // Choose steady-state based on declared buffer usage
      recorder->RequireResourceState(
        *br.dst, UsageToTargetState(br.dst->GetUsage()));
      recorder->FlushBarriers();
    }

    // Defer unmap-then-release for the batched staging buffer
    DeferUnmapThenRelease(g->GetDeferredReclaimer(), staging.buffer);
    auto queue = g->GetCommandQueue(key);
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

// Extend in future for textures and batching.

auto UploadCoordinator::RetireCompleted() -> void
{
  auto g = graphics_.get();
  if (!g)
    return;
  EnsureQueues(*g);
  // Poll the same queue role used for uploads (transfer preferred)
  const auto key = ChooseUploadQueueKey(*g);
  if (auto q = g->GetCommandQueue(key); q) {
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

auto UploadCoordinator::Flush() -> void
{
  auto g = graphics_.get();
  if (!g)
    return;
  EnsureQueues(*g);
  g->SubmitDeferredCommandLists();
}

} // namespace oxygen::engine::upload
