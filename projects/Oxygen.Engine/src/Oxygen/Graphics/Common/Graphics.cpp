//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <mutex>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Bindless.h>
#include <Oxygen/Graphics/Common/Internal/FramebufferImpl.h>
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::Graphics;
using oxygen::graphics::internal::Bindless;
using oxygen::graphics::internal::QueueManager;

Graphics::Graphics(const std::string_view name)
{
  AddComponent<ObjectMetaData>(name);
  // Create backend-agnostic Bindless component now; allocator will be set by
  // backend later after device is created.
  AddComponent<Bindless>();
  // Install QueueManager component for common graphics queue management.
  AddComponent<QueueManager>();
}

Graphics::~Graphics() = default;

auto Graphics::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  DLOG_F(INFO, "Graphics Live Object activating...");
  return OpenNursery(nursery_, std::move(started));
}

auto Graphics::Run() -> void
{
  DLOG_F(INFO, "Starting Graphics backend async tasks...");
}

auto Graphics::IsRunning() const -> bool { return nursery_ != nullptr; }

auto Graphics::Stop() -> void
{
  // Flush all command queues
  FlushCommandQueues();

  // Stop all valid renderers
  auto it = renderers_.begin();
  while (it != renderers_.end()) {
    if (const auto renderer = it->lock()) {
      renderer->Stop();
      ++it;
    }
  }
  renderers_.clear();

  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }

  // Clear command list pool
  {
    std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
    command_list_pool_.clear();
  }

  command_queues_.clear();

  DLOG_F(INFO, "Graphics Live Object stopped");
}

auto Graphics::SetDescriptorAllocator(
  std::unique_ptr<graphics::DescriptorAllocator> allocator) -> void
{
  CHECK_NOTNULL_F(allocator);
  DCHECK_F(HasComponent<Bindless>(),
    "Bindless component must exist on Graphics before setting allocator");
  GetComponent<Bindless>().SetAllocator(std::move(allocator));
}

auto Graphics::CreateCommandQueues(
  const graphics::QueuesStrategy& queue_strategy) -> void
{
  // Delegate queue management to the installed QueueManager component which
  // will call back to this backend's CreateCommandQueue hook when it needs to
  // instantiate actual CommandQueue objects.
  auto& qm = GetComponent<oxygen::graphics::internal::QueueManager>();
  qm.CreateQueues(queue_strategy,
    [this](const graphics::QueueKey& k,
      graphics::QueueRole r) -> std::shared_ptr<graphics::CommandQueue> {
      return this->CreateCommandQueue(k, r);
    });
  // Mirror the manager's name-based map into command_queues_ for backwards
  // compatibility with existing code that inspects `command_queues_`.
  command_queues_.clear();
  for (const auto& spec : queue_strategy.Specifications()) {
    const auto q = qm.GetQueueByName(spec.key);
    if (q) {
      command_queues_.emplace(spec.key, q);
    }
  }
}

auto Graphics::FlushCommandQueues() -> void
{
  // Forward to the QueueManager which enumerates unique queues safely.
  auto& qm = GetComponent<oxygen::graphics::internal::QueueManager>();
  qm.ForEachQueue([](graphics::CommandQueue& q) { q.Flush(); });
}

auto Graphics::GetCommandQueue(const graphics::QueueKey& key) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<oxygen::graphics::internal::QueueManager>();
  return qm.GetQueueByName(key);
}

auto Graphics::GetCommandQueue(const graphics::QueueRole role) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<oxygen::graphics::internal::QueueManager>();
  return qm.GetQueueByRole(role);
}

auto Graphics::CreateRenderController(const std::string_view name,
  std::weak_ptr<graphics::Surface> surface,
  const frame::SlotCount frames_in_flight)
  -> std::shared_ptr<graphics::RenderController>
{
  // Create the RenderController object
  auto renderer
    = CreateRendererImpl(name, std::move(surface), frames_in_flight);
  CHECK_NOTNULL_F(renderer, "Failed to create renderer");

  // Wrap the RenderController in a shared_ptr with a custom deleter
  auto renderer_with_deleter = std::shared_ptr<graphics::RenderController>(
    renderer.release(), [this](graphics::RenderController* ptr) {
      // Remove the RenderController from the renderers_ collection
      const auto it = std::ranges::remove_if(renderers_,
        [ptr](const std::weak_ptr<graphics::RenderController>& weak_renderer) {
          const auto shared_renderer = weak_renderer.lock();
          return !shared_renderer || shared_renderer.get() == ptr;
        }).begin();
      renderers_.erase(it, renderers_.end());

      // Delete the RenderController
      LOG_SCOPE_F(INFO, "Destroy RenderController");
      delete ptr;
    });

  // Add a weak_ptr to the renderers_ collection
  renderers_.emplace_back(renderer_with_deleter);

  return renderer_with_deleter;
}

auto Graphics::AcquireCommandList(
  graphics::QueueRole queue_role, const std::string_view command_list_name)
  -> std::shared_ptr<graphics::CommandList>
{
  // Acquire or create a command list
  std::unique_ptr<graphics::CommandList> cmd_list;
  {
    std::lock_guard lock(command_list_pool_mutex_);

    if (auto& pool = command_list_pool_[queue_role]; pool.empty()) {
      // Create a new command list if pool is empty
      cmd_list = CreateCommandListImpl(queue_role, command_list_name);
    } else {
      // Take one from the pool
      cmd_list = std::move(pool.back());
      pool.pop_back();
      cmd_list->SetName(command_list_name);
    }
  }

  // Create a shared_ptr with custom deleter that returns the command list to
  // the pool
  return { cmd_list.get(),
    [this, queue_role, cmd_list_raw = cmd_list.release()](
      graphics::CommandList*) mutable {
      cmd_list_raw->SetName("Recycled Command List");
      // Create a new unique_ptr that owns the command list
      auto recycled_cmd_list
        = std::unique_ptr<graphics::CommandList>(cmd_list_raw);

      // Return to pool
      std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
      command_list_pool_[queue_role].push_back(std::move(recycled_cmd_list));
    } };

  // The Original shared_ptr will be destroyed, but the command list is now
  // managed by the custom deleter and will be returned to the pool when the
  // returned shared_ptr is destroyed
}

auto Graphics::GetDescriptorAllocator() const
  -> const graphics::DescriptorAllocator&
{
  return GetComponent<Bindless>().GetAllocator();
}

auto Graphics::GetDescriptorAllocator() -> graphics::DescriptorAllocator&
{
  return const_cast<graphics::DescriptorAllocator&>(
    std::as_const(*this).GetDescriptorAllocator());
}

auto Graphics::GetResourceRegistry() const -> const graphics::ResourceRegistry&
{
  return GetComponent<Bindless>().GetRegistry();
}

auto Graphics::GetResourceRegistry() -> graphics::ResourceRegistry&
{
  return const_cast<graphics::ResourceRegistry&>(
    std::as_const(*this).GetResourceRegistry());
}

auto Graphics::CreateFramebuffer(const graphics::FramebufferDesc& desc)
  -> std::shared_ptr<graphics::Framebuffer>
{
  return std::make_shared<graphics::internal::FramebufferImpl>(
    desc, weak_from_this());
}
