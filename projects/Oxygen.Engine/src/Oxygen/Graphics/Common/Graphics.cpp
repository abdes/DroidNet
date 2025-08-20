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
#include <Oxygen/Graphics/Common/Detail/Bindless.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::Graphics;

Graphics::Graphics(const std::string_view name)
{
  AddComponent<ObjectMetaData>(name);
  // Create backend-agnostic Bindless component now; allocator will be set by
  // backend later after device is created.
  AddComponent<oxygen::graphics::detail::Bindless>();
}

Graphics::~Graphics() = default;

auto Graphics::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  DLOG_F(INFO, "Graphics Live Object activating...");
  return OpenNursery(nursery_, std::move(started));
}

void Graphics::Run()
{
  DLOG_F(INFO, "Starting Graphics backend async tasks...");
}

auto Graphics::IsRunning() const -> bool { return nursery_ != nullptr; }

void Graphics::Stop()
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
  using oxygen::graphics::detail::Bindless;
  CHECK_NOTNULL_F(allocator);
  DCHECK_F(HasComponent<Bindless>(),
    "Bindless component must exist on Graphics before setting allocator");
  GetComponent<Bindless>().SetAllocator(std::move(allocator));
}

void Graphics::CreateCommandQueues(
  const graphics::QueueStrategy& queue_strategy)
{
  LOG_IF_F(INFO, !command_queues_.empty(),
    "Re-creating command queues for the graphics backend");
  command_queues_.clear();

  const auto queue_specs = queue_strategy.Specifications();
  std::unordered_map<std::string, std::shared_ptr<graphics::CommandQueue>>
    temp_queues;

  try {
    for (const auto& spec : queue_specs) {
      auto queue
        = CreateCommandQueue(spec.name, spec.role, spec.allocation_preference);
      // If CreateCommandQueue does not throw, queue is guaranteed to be not
      // null.
      temp_queues.emplace(spec.name, std::move(queue));
    }
    command_queues_ = std::move(temp_queues);
  } catch (...) {
    // Destroy all previously created queues
    temp_queues.clear();
    throw;
  }
}

void Graphics::FlushCommandQueues()
{
  LOG_SCOPE_F(1, "Flushing all command queues");
  for (const auto& queue : command_queues_ | std::views::values) {
    DCHECK_NOTNULL_F(queue);
    queue->Flush();
  }
}

auto Graphics::GetCommandQueue(const std::string_view name) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  if (const auto it = std::ranges::find(
        command_queues_, name, [](const auto& pair) { return pair.first; });
    it != command_queues_.end()) {
    return it->second;
  }

  LOG_F(WARNING, "Command queue '{}' not found", name);
  return {};
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
  return GetComponent<graphics::detail::Bindless>().GetAllocator();
}

auto Graphics::GetDescriptorAllocator() -> graphics::DescriptorAllocator&
{
  return const_cast<graphics::DescriptorAllocator&>(
    std::as_const(*this).GetDescriptorAllocator());
}

auto Graphics::GetResourceRegistry() const -> const graphics::ResourceRegistry&
{
  return GetComponent<graphics::detail::Bindless>().GetRegistry();
}

auto Graphics::GetResourceRegistry() -> graphics::ResourceRegistry&
{
  return const_cast<graphics::ResourceRegistry&>(
    std::as_const(*this).GetResourceRegistry());
}
