//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Headless/Bindless/AllocationStrategy.h>
#include <Oxygen/Graphics/Headless/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/CommandList.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Internal/EngineShaders.h>
#include <Oxygen/Graphics/Headless/Internal/QueueManager.h>
#include <Oxygen/Graphics/Headless/Surface.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

Graphics::Graphics(const SerializedBackendConfig& /*config*/)
  : oxygen::Graphics("HeadlessGraphics")
{
  // Install EngineShaders component so shader cache is stored in composition
  AddComponent<internal::EngineShaders>();

  // Install QueueManager component to manage command queues
  AddComponent<internal::QueueManager>();

  // Initialize global Bindless allocator at the device level
  {
    auto allocator = std::make_unique<bindless::DescriptorAllocator>(
      std::make_shared<bindless::AllocationStrategy>());
    SetDescriptorAllocator(std::move(allocator));
  }

  LOG_F(INFO, "Headless Graphics instance created");
}

auto Graphics::CreateTexture(const TextureDesc& desc) const
  -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc);
}

auto Graphics::CreateTextureFromNativeObject(const TextureDesc& desc,
  const NativeObject& /*native*/) const -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc);
}

auto Graphics::CreateBuffer(const BufferDesc& desc) const
  -> std::shared_ptr<graphics::Buffer>
{
  auto b = std::make_shared<Buffer>(desc);
  return b;
}

auto Graphics::CreateCommandQueue(std::string_view /*queue_name*/,
  QueueRole /*role*/, QueueAllocationPreference /*allocation_preference*/)
  -> std::shared_ptr<graphics::CommandQueue>
{
  // This is a no-op, because headless manages queues by itself in the
  // QueueManager. See CreateCommandQueues().
  ABORT_F("Headless: CreateCommandQueue should not be called");
}

// In headless the queue creation policy lives in internal::QueueManager; the
// backend-level CreateCommandQueues is intentionally a no-op because queues
// are created on demand (or by QueueManager strategies) rather than by the
// base class initializer.
auto Graphics::CreateCommandQueues(
  const graphics::QueueStrategy& queue_strategy) -> void
{
  auto& qm = GetComponent<internal::QueueManager>();
  qm.CreateQueues(queue_strategy);
}

// Flush headless command queues: ensure all pending submissions are processed
// and queue-side recorded commands (signals/waits) are executed. This method
// walks all known queues from the QueueManager and waits for their pending
// submissions to complete, then invokes a queue flush operation if available.
auto Graphics::FlushCommandQueues() -> void
{
  // Use the installed QueueManager component for queue enumeration.
  auto& qm = GetComponent<internal::QueueManager>();
  qm.ForEachQueue([](graphics::CommandQueue& q) { q.Flush(); });
}

auto Graphics::CreateSurface(std::weak_ptr<platform::Window> /*window_weak*/,
  std::shared_ptr<graphics::CommandQueue> /*command_queue*/) const
  -> std::shared_ptr<Surface>
{
  return std::make_shared<HeadlessSurface>("headless-surface");
}

auto Graphics::GetShader(std::string_view unique_id) const
  -> std::shared_ptr<IShaderByteCode>
{
  auto& shaders = GetComponent<internal::EngineShaders>();
  return shaders.GetShader(unique_id);
}

auto Graphics::CreateCommandListImpl(QueueRole role,
  std::string_view command_list_name) -> std::unique_ptr<graphics::CommandList>
{
  LOG_F(INFO, "Headless CreateCommandList requested: role={} name={}",
    nostd::to_string(role), command_list_name);
  const auto name = command_list_name.empty()
    ? std::string_view("headless-cmdlist")
    : command_list_name;
  return std::make_unique<headless::CommandList>(name, role);
}

auto Graphics::CreateCommandRecorder(
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
  -> std::unique_ptr<graphics::CommandRecorder>
{
  return std::make_unique<headless::CommandRecorder>(
    std::move(command_list), target_queue);
}

auto Graphics::GetCommandQueue(std::string_view name) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<internal::QueueManager>();
  return qm.GetQueueByName(name);
}

auto Graphics::AcquireCommandRecorder(
  std::shared_ptr<graphics::CommandQueue> queue,
  std::shared_ptr<graphics::CommandList> command_list,
  bool immediate_submission) -> std::unique_ptr<graphics::CommandRecorder,
  std::function<void(graphics::CommandRecorder*)>>
{
  // Create headless recorder via backend factory. We reuse the
  // CreateCommandRecorder hook provided by the common Graphics interface; for
  // headless this should produce a headless::CommandRecorder instance.
  auto recorder
    = CreateCommandRecorder(command_list, observer_ptr { queue.get() });
  if (!recorder) {
    return nullptr;
  }
  recorder->Begin();

  // The deleter will either submit immediately or capture the completed
  // command list for deferred submission. We need to ensure thread-safe
  // access to pending_cmd_lists_. The deleter captures a weak pointer to
  // `this` to avoid dangling references if Graphics is destroyed.
  auto self = this;
  return {
    recorder.release(),
    [self, cmd_list = std::move(command_list), immediate_submission](
      graphics::CommandRecorder* rec) mutable {
      if (!rec) {
        return;
      }
      try {
        if (auto completed_cmd = rec->End(); completed_cmd != nullptr) {
          auto target_queue = rec->GetTargetQueue();
          DCHECK_NOTNULL_F(target_queue);
          if (immediate_submission) {
            LOG_F(INFO, "Immediate Submission -> calling rec->End()");
            LOG_F(INFO, "Deleter: submitting the command list to the queue");
            target_queue->Submit(*completed_cmd);
            LOG_F(INFO, "Deleter: calling cmd_list->OnSubmitted()");
            cmd_list->OnSubmitted();
          } else {
            // Deferred: store the completed command list so it can be
            // submitted later by SubmitDeferredCommandLists(). Use the
            // Graphics instance mutex to protect the pending list.
            try {
              std::lock_guard lk(self->pending_cmd_lists_mutex_);
              self->pending_cmd_lists_.emplace_back(std::move(completed_cmd));
            } catch (const std::exception& e) {
              LOG_F(
                ERROR, "Failed to store deferred command list: {}", e.what());
            }
          }
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "Exception in headless recorder cleanup: {}", ex.what());
      }
      delete rec;
    },
  };
}

auto Graphics::SubmitDeferredCommandLists() -> void
{
  std::vector<std::shared_ptr<graphics::CommandList>> lists_to_submit;
  {
    std::lock_guard lk(pending_cmd_lists_mutex_);
    if (pending_cmd_lists_.empty()) {
      return;
    }
    lists_to_submit.swap(pending_cmd_lists_);
  }

  for (auto& cmd_list : lists_to_submit) {
    if (!cmd_list) {
      continue;
    }
    // Find the target queue via the queue manager or the common Graphics
    // lookup. For headless the recorded command lists encode the queue role
    // they should be submitted to; ask the QueueManager for an appropriate
    // queue instance. We'll attempt to use the Graphics::GetCommandQueue by
    // role name fallback; if not found, try to find any queue with matching
    // role via the installed QueueManager.
    bool submitted = false;
    auto& qm = GetComponent<internal::QueueManager>();
    qm.ForEachQueue([&](graphics::CommandQueue& q) {
      if (submitted) {
        return;
      }
      if (q.GetQueueRole() == cmd_list->GetQueueRole()) {
        try {
          q.Submit(*cmd_list);
          cmd_list->OnSubmitted();
        } catch (const std::exception& e) {
          LOG_F(ERROR, "SubmitDeferredCommandLists: failed to submit: {}",
            e.what());
          throw;
        }
        submitted = true;
      }
    });

    if (!submitted) {
      LOG_F(WARNING,
        "SubmitDeferredCommandLists: no matching queue for role={}",
        nostd::to_string(cmd_list->GetQueueRole()));
    }
  }
}

auto Graphics::CreateRendererImpl(std::string_view /*name*/,
  std::weak_ptr<Surface> /*surface*/, frame::SlotCount /*frames_in_flight*/)
  -> std::unique_ptr<RenderController>
{
  return nullptr;
}

} // namespace oxygen::graphics::headless
