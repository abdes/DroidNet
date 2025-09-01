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
#include <Oxygen/Graphics/Headless/Internal/Commander.h>
#include <Oxygen/Graphics/Headless/Internal/EngineShaders.h>
#include <Oxygen/Graphics/Headless/Surface.h>
#include <Oxygen/Graphics/Headless/Texture.h>

//===----------------------------------------------------------------------===//
// DescriptorAllocator Component
//===----------------------------------------------------------------------===//

namespace {

namespace hb = oxygen::graphics::headless::bindless;

class DescriptorAllocatorComponent : public oxygen::Component {
  OXYGEN_COMPONENT(DescriptorAllocatorComponent)

public:
  explicit DescriptorAllocatorComponent()
    : allocator_(std::make_unique<hb::DescriptorAllocator>(
        std::make_shared<hb::AllocationStrategy>()))
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocatorComponent)
  OXYGEN_DEFAULT_MOVABLE(DescriptorAllocatorComponent)

  ~DescriptorAllocatorComponent() override = default;

  [[nodiscard]] auto GetAllocator() const -> const auto& { return *allocator_; }

private:
  std::unique_ptr<hb::DescriptorAllocator> allocator_ {};
};

} // namespace

//===----------------------------------------------------------------------===//
// Graphics implementation
//===----------------------------------------------------------------------===//

namespace oxygen::graphics::headless {

Graphics::Graphics(const SerializedBackendConfig& /*config*/)
  : oxygen::Graphics("HeadlessGraphics")
{
  AddComponent<internal::EngineShaders>();
  AddComponent<internal::Commander>();
  AddComponent<DescriptorAllocatorComponent>();

  LOG_F(INFO, "Headless Graphics instance created");
}

auto Graphics::GetDescriptorAllocator() const -> const DescriptorAllocator&
{
  return GetComponent<DescriptorAllocatorComponent>().GetAllocator();
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

auto Graphics::CreateCommandQueue(const QueueKey& queue_key, QueueRole role)
  -> std::shared_ptr<graphics::CommandQueue>
{
  return std::make_shared<CommandQueue>(queue_key.get(), role);
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
    ? std::string_view("headless-cmd-list")
    : command_list_name;
  return std::make_unique<CommandList>(name, role);
}

auto Graphics::CreateCommandRecorder(
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
  -> std::unique_ptr<graphics::CommandRecorder>
{
  return std::make_unique<CommandRecorder>(
    std::move(command_list), target_queue);
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
  auto& cmdr = GetComponent<internal::Commander>();
  return cmdr.PrepareCommandRecorder(
    std::move(recorder), std::move(command_list), immediate_submission);
}

auto Graphics::SubmitDeferredCommandLists() -> void
{
  GetComponent<internal::Commander>().SubmitDeferredCommandLists();
}

auto Graphics::CreateRendererImpl(std::string_view /*name*/,
  std::weak_ptr<Surface> /*surface*/, frame::SlotCount /*frames_in_flight*/)
  -> std::unique_ptr<RenderController>
{
  return nullptr;
}

} // namespace oxygen::graphics::headless
