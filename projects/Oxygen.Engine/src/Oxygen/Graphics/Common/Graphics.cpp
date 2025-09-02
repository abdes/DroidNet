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
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/Internal/FramebufferImpl.h>
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::Graphics;
using oxygen::graphics::detail::DeferredReclaimer;
using oxygen::graphics::internal::Commander;
using oxygen::graphics::internal::CommandListPool;
using oxygen::graphics::internal::QueueManager;

namespace {

class ResourceRegistryComponent : public oxygen::Component {
  OXYGEN_COMPONENT(ResourceRegistryComponent)

public:
  explicit ResourceRegistryComponent(std::string_view name)
    : registry_(std::make_unique<oxygen::graphics::ResourceRegistry>(name))
  {
  }

  [[nodiscard]] auto GetRegistry() const -> const auto& { return *registry_; }

  OXYGEN_MAKE_NON_COPYABLE(ResourceRegistryComponent)
  OXYGEN_DEFAULT_MOVABLE(ResourceRegistryComponent)

  ~ResourceRegistryComponent() override = default;

private:
  std::unique_ptr<oxygen::graphics::ResourceRegistry> registry_ {};
};

} // namespace

Graphics::Graphics(const std::string_view name)
{
  AddComponent<ObjectMetaData>(name);
  AddComponent<ResourceRegistryComponent>(name);
  AddComponent<QueueManager>();
  AddComponent<Commander>();
  AddComponent<CommandListPool>(
    [this](graphics::QueueRole role,
      std::string_view name) -> std::unique_ptr<graphics::CommandList> {
      return this->CreateCommandListImpl(role, name);
    });
  AddComponent<DeferredReclaimer>();
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

  // Process All deferred releases
  auto& reclaimer = GetComponent<DeferredReclaimer>();
  reclaimer.ProcessAllDeferredReleases();

  // Clear the CommandList pool
  auto& command_list_pool = GetComponent<CommandListPool>();
  command_list_pool.Clear();

  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }

  DLOG_F(INFO, "Graphics Live Object stopped");
}

auto Graphics::BeginFrame(
  frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
{
  auto& reclaimer = GetComponent<DeferredReclaimer>();
  reclaimer.OnBeginFrame(frame_slot);
}

auto Graphics::EndFrame(
  frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
{
  // TODO: Implement frame end logic
}

auto Graphics::PresentSurfaces(
  const std::vector<std::shared_ptr<graphics::Surface>>& surfaces) -> void
{
  LOG_SCOPE_FUNCTION(0);

  for (const auto& surface : surfaces) {
    try {
      surface->Present();
    } catch (const std::exception& e) {
      LOG_F(WARNING, "Present on surface `{}` failed; frame discarded: {}",
        surface->GetName(), e.what());
    }
  }
}

auto Graphics::CreateCommandQueues(
  const graphics::QueuesStrategy& queue_strategy) -> void
{
  // Delegate queue management to the installed QueueManager component which
  // will call back to this backend's CreateCommandQueue hook when it needs to
  // instantiate actual CommandQueue objects.
  auto& qm = GetComponent<QueueManager>();
  qm.CreateQueues(queue_strategy,
    [this](const graphics::QueueKey& key, const graphics::QueueRole role)
      -> std::shared_ptr<graphics::CommandQueue> {
      return this->CreateCommandQueue(key, role);
    });
}

auto Graphics::FlushCommandQueues() -> void
{
  // Forward to the QueueManager which enumerates unique queues safely.
  auto& qm = GetComponent<QueueManager>();
  qm.ForEachQueue([](const graphics::CommandQueue& q) { q.Flush(); });
}

auto Graphics::GetCommandQueue(const graphics::QueueKey& key) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
  return qm.GetQueueByName(key);
}

auto Graphics::GetCommandQueue(const graphics::QueueRole role) const
  -> std::shared_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
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

auto Graphics::AcquireCommandRecorder(
  const observer_ptr<graphics::CommandQueue> queue,
  std::shared_ptr<graphics::CommandList> command_list,
  const bool immediate_submission) -> std::unique_ptr<graphics::CommandRecorder,
  std::function<void(graphics::CommandRecorder*)>>
{
  // Create backend recorder and forward to the Commander component which will
  // wrap it with the appropriate deleter behavior.
  auto recorder = CreateCommandRecorder(command_list, queue);
  auto& cmdr = GetComponent<Commander>();
  return cmdr.PrepareCommandRecorder(
    std::move(recorder), std::move(command_list), immediate_submission);
}

auto Graphics::SubmitDeferredCommandLists() -> void
{
  GetComponent<Commander>().SubmitDeferredCommandLists();
}

auto Graphics::AcquireCommandList(
  graphics::QueueRole queue_role, const std::string_view command_list_name)
  -> std::shared_ptr<graphics::CommandList>
{
  auto& command_list_pool = GetComponent<CommandListPool>();
  return command_list_pool.AcquireCommandList(queue_role, command_list_name);
}

auto Graphics::GetDescriptorAllocator() -> graphics::DescriptorAllocator&
{
  return const_cast<graphics::DescriptorAllocator&>(
    std::as_const(*this).GetDescriptorAllocator());
}

auto Graphics::GetResourceRegistry() const -> const graphics::ResourceRegistry&
{
  return GetComponent<ResourceRegistryComponent>().GetRegistry();
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
