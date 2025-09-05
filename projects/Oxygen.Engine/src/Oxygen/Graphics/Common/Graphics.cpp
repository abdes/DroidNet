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
  AddComponent<ObjectMetadata>(name);
  AddComponent<ResourceRegistryComponent>(name);
  AddComponent<QueueManager>();
  // CommandListPool before DeferredReclaimer
  AddComponent<CommandListPool>(
    [this](graphics::QueueRole role,
      std::string_view name) -> std::unique_ptr<graphics::CommandList> {
      return this->CreateCommandListImpl(role, name);
    });
  // DeferredReclaimer must be created before Commander because Commander
  // depends on oxygen::graphics::detail::DeferredReclaimer.
  AddComponent<DeferredReclaimer>();
  AddComponent<Commander>();
}

Graphics::~Graphics()
{
  // Clear the CommandList pool
  auto& command_list_pool = GetComponent<CommandListPool>();
  command_list_pool.Clear();
}

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

auto Graphics::Shutdown() -> void
{
  DLOG_SCOPE_F(1, "Graphics::Shutdown");
  // Flush all command queues
  FlushCommandQueues();

  // Process All deferred releases
  auto& reclaimer = GetComponent<DeferredReclaimer>();
  reclaimer.ProcessAllDeferredReleases();
}

auto Graphics::Stop() -> void
{
  if (!IsRunning()) {
    return;
  }

  nursery_->Cancel();
  DLOG_F(INFO, "Graphics Live Object stopped");
}

auto Graphics::BeginFrame(
  frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
{
  // Flush all command queues to ensure GPU work is submitted before releasing
  // resources
  FlushCommandQueues();

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
  -> observer_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
  return qm.GetQueueByName(key);
}

auto Graphics::GetCommandQueue(const graphics::QueueRole role) const
  -> observer_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
  return qm.GetQueueByRole(role);
}

auto Graphics::AcquireCommandRecorder(const graphics::QueueKey& queue_key,
  const std::string_view command_list_name, const bool immediate_submission)
  -> std::unique_ptr<graphics::CommandRecorder,
    std::function<void(graphics::CommandRecorder*)>>
{
  // Get the command queue from the queue key
  auto queue = GetCommandQueue(queue_key);
  DCHECK_NOTNULL_F(queue, "Failed to get command queue for key");

  // Acquire a command list
  auto command_list
    = AcquireCommandList(queue->GetQueueRole(), command_list_name);
  DCHECK_NOTNULL_F(command_list, "Failed to acquire command list");

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
auto Graphics::GetDeferredReclaimer() -> graphics::detail::DeferredReclaimer&
{
  return GetComponent<graphics::detail::DeferredReclaimer>();
}

auto Graphics::CreateFramebuffer(const graphics::FramebufferDesc& desc)
  -> std::shared_ptr<graphics::Framebuffer>
{
  return std::make_shared<graphics::internal::FramebufferImpl>(
    desc, weak_from_this());
}
