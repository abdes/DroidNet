//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <memory>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/OxCo/Co.h>

using oxygen::graphics::RenderController;
using oxygen::graphics::RenderPass;
using oxygen::graphics::detail::RenderThread;

//! Default constructor, sets the object name.
RenderController::RenderController(std::string_view name,
  std::weak_ptr<Graphics> gfx_weak, std::weak_ptr<Surface> surface_weak,
  frame::SlotCount frames_in_flight)
  : gfx_weak_(std::move(gfx_weak))
  , surface_weak_(std::move(surface_weak))
  , frame_count_(frames_in_flight)
  , frames_(std::make_unique<Frame[]>(frame_count_.get()))
{
  CHECK_F(!surface_weak_.expired(),
    "RenderController cannot be created with a null Surface");
  DCHECK_F(!gfx_weak_.expired(),
    "RenderController cannot be created with an expired Graphics backend "
    "pointer");

  // Initialize all frame values to 0
  for (uint32_t i = 0; i < frame_count_.get(); ++i) {
    frames_[i] = Frame {};
  }

  AddComponent<ObjectMetaData>(name);
  AddComponent<RenderThread>(
    frames_in_flight, [this]() { this->BeginFrame(); },
    [this]() { this->EndFrame(); });
}

RenderController::~RenderController() { Stop(); }

auto RenderController::GetGraphics() -> Graphics&
{
  CHECK_F(!gfx_weak_.expired(),
    "Unexpected use of RenderController when the Graphics backend is no longer "
    "valid");
  return *gfx_weak_.lock();
}

auto RenderController::GetGraphics() const -> const Graphics&
{
  return const_cast<RenderController*>(this)->GetGraphics();
}

auto RenderController::GetDescriptorAllocator() const
  -> const DescriptorAllocator&
{
  return GetGraphics().GetDescriptorAllocator();
}

auto RenderController::GetDescriptorAllocator() -> DescriptorAllocator&
{
  return const_cast<DescriptorAllocator&>(
    std::as_const(*this).GetDescriptorAllocator());
}

auto RenderController::GetResourceRegistry() const -> const ResourceRegistry&
{
  return GetGraphics().GetResourceRegistry();
}

auto RenderController::GetResourceRegistry() -> ResourceRegistry&
{
  return const_cast<ResourceRegistry&>(
    std::as_const(*this).GetResourceRegistry());
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto RenderController::Stop() -> void
{
  GetComponent<RenderThread>().Stop();
  per_frame_resource_manager_.OnRendererShutdown();
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto RenderController::Submit(FrameRenderTask task) -> void
{
  GetComponent<RenderThread>().Submit(std::move(task));
}

auto RenderController::AcquireCommandRecorder(const std::string_view queue_name,
  const std::string_view command_list_name, bool immediate_submission)
  -> std::unique_ptr<CommandRecorder, std::function<void(CommandRecorder*)>>
{
  CHECK_F(!gfx_weak_.expired(),
    "Unexpected use of RenderController when the Graphics backend is no longer "
    "valid");
  const auto gfx = gfx_weak_.lock();

  const auto queue = gfx->GetCommandQueue(queue_name);
  if (!queue) {
    LOG_F(ERROR, "Command queue '{}' not found", queue_name);
    return nullptr;
  }

  // Get a command list with automatic cleanup
  auto cmd_list
    = gfx->AcquireCommandList(queue->GetQueueRole(), command_list_name);
  if (!cmd_list) {
    return nullptr;
  }

  // Create a command recorder for this command list
  auto recorder = CreateCommandRecorder(cmd_list, observer_ptr { queue.get() });
  if (!recorder) {
    return nullptr;
  }
  recorder->Begin();

  return { recorder.release(),
    [self_weak = weak_from_this(), cmd_list = std::move(cmd_list),
      queue = queue.get(), immediate_submission](CommandRecorder* rec) mutable {
      if (rec == nullptr) {
        return;
      }
      if (self_weak.expired()) {
        LOG_F(ERROR, "RenderController is no longer valid");
        delete rec;
        return;
      }
      auto renderer = self_weak.lock();
      try {
        if (immediate_submission) {
          if (auto completed_cmd = rec->End(); completed_cmd != nullptr) {
            auto target_queue = rec->GetTargetQueue();
            DCHECK_NOTNULL_F(target_queue);
            target_queue->Submit(*completed_cmd);
            completed_cmd->OnSubmitted();
            const uint64_t timeline_value = target_queue->Signal();
            const uint32_t frame_idx = renderer->CurrentFrameIndex().get();
            auto& [timeline_values, pending_command_lists]
              = renderer->frames_[frame_idx];
            timeline_values[queue] = timeline_value;
            pending_command_lists.emplace_back(cmd_list, queue);
          }
        } else {
          // Deferred: just end, don't submit. Add to pending_command_lists for
          // later flush.
          if (auto completed_cmd = rec->End(); completed_cmd != nullptr) {
            const uint32_t frame_idx = renderer->CurrentFrameIndex().get();
            auto& [timeline_values, pending_command_lists]
              = renderer->frames_[frame_idx];
            pending_command_lists.emplace_back(std::move(cmd_list), queue);
          }
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "Exception in command recorder cleanup: {}", ex.what());
      }
      delete rec;
    } };
}

auto RenderController::FlushPendingCommandLists() -> void
{
  auto& [timeline_values, pending_command_lists]
    = frames_[CurrentFrameIndex().get()];
  if (pending_command_lists.empty())
    return;
  if (!gfx_weak_.expired()) {
    auto it = pending_command_lists.begin();
    while (it != pending_command_lists.end()) {
      CommandQueue* current_queue = it->second;
      std::vector<CommandList*> batch;
      auto batch_start = it;
      // Collect contiguous command lists for the same queue, only if closed and
      // not submitted
      while (it != pending_command_lists.end() && it->second == current_queue) {
        if (it->first && it->first->IsClosed() && !it->first->IsSubmitted()) {
          batch.push_back(it->first.get());
        }
        ++it;
      }
      if (!batch.empty()) {
        current_queue->Submit(std::span<CommandList*> { batch });
        uint64_t timeline_value = current_queue->Signal();
        timeline_values[current_queue] = timeline_value;
        for (auto* cmd_list : batch) {
          cmd_list->OnSubmitted();
        }
      }
    }
  }
  // Do not clear pending_command_lists or timeline_values here; this is done in
  // BeginFrame().
}

auto RenderController::BeginFrame() -> void
{
  CHECK_F(!gfx_weak_.expired(),
    "Unexpected use of RenderController when the Graphics backend is no longer "
    "valid");

  if (surface_weak_.expired()) {
    throw std::runtime_error("Cannot BeginFrame when surface is not valid");
  }

  auto& [timeline_values, pending_command_lists]
    = frames_[CurrentFrameIndex().get()];

  LOG_SCOPE_FUNCTION(1);
  DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

  // NB: Must handle surface resize early as it may affect the current frame
  // index.

  if (const auto surface = surface_weak_.lock(); surface->ShouldResize()) {
    // This will flush the command queues and wait for all pending work to
    // finish for all frames, release all deferred resources and resize the
    // swapchain.
    HandleSurfaceResize(*surface);
    DLOG_F(1, "Frame index after resize: {}", CurrentFrameIndex());
  } else {
    // Wait for the GPU to finish executing the previous frame (only for the
    // current frame index).
    for (const auto& [queue, fence_value] : timeline_values) {
      const auto gfx = gfx_weak_.lock();
      DCHECK_NOTNULL_F(queue, "Command queue is null");
      if (queue) {
        queue->Wait(fence_value);
      }
    }

    // Process all deferred releases for the current frame
    per_frame_resource_manager_.OnBeginFrame(CurrentFrameIndex());
  }

  // Release all completed command lists and call OnExecuted
  for (const auto& cmd_list : pending_command_lists | std::views::keys) {
    cmd_list->OnExecuted();
  }
  pending_command_lists.clear();
  timeline_values.clear();
}

auto RenderController::EndFrame() -> void
{
  CHECK_F(
    !surface_weak_.expired(), "Cannot BeginFrame when surface is not valid");

  LOG_SCOPE_FUNCTION(1);
  DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

  // Always flush before presenting
  FlushPendingCommandLists();

  const auto surface = surface_weak_.lock();
  try {
    surface->Present();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "Present on surface `{}` failed; frame discarded: {}",
      surface->GetName(), e.what());
  }

  current_frame_slot_
    = frame::Slot { (current_frame_slot_.get() + 1) % frame_count_.get() };
}

auto RenderController::HandleSurfaceResize(Surface& surface) -> void
{
  DCHECK_F(!gfx_weak_.expired(),
    "Unexpected use of RenderController when the Graphics backend is no longer "
    "valid");

  // Collect all queues that have pending work across any frame
  std::unordered_set<CommandQueue*> active_queues;

  // Check all frames for pending work
  for (uint32_t i = 0; i < frame_count_.get(); ++i) {
    if (i != CurrentFrameIndex().get()) {
      // Already processed current frame above
      for (const auto& queue : frames_[i].timeline_values | std::views::keys) {
        active_queues.insert(queue);
      }
    }
  }

  // Only flush queues with pending work from this renderer
  if (!active_queues.empty()) {
    const auto gfx = gfx_weak_.lock();
    // We don't really care about the order of flushing queues.
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
    for (const auto& queue : active_queues) {
      DLOG_F(INFO, "Flushing queue '{}' during resize", queue->GetName());
      queue->Flush();
    }
  }

  // Process all deferred releases for all frames since we have flushed all
  // pending work for all frames and we are going to reset the swapchain.
  per_frame_resource_manager_.ProcessAllDeferredReleases();

  surface.Resize();
  current_frame_slot_ = frame::Slot { surface.GetCurrentBackBufferIndex() };
}
