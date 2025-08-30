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
#include <Oxygen/Graphics/Headless/CommandQueue.h>
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
[[nodiscard]] auto Graphics::CreateTexture(const TextureDesc& desc) const
  -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc);
}
[[nodiscard]] auto Graphics::CreateTextureFromNativeObject(
  const TextureDesc& desc, const NativeObject& /*native*/) const
  -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc);
}
[[nodiscard]] auto Graphics::CreateBuffer(const BufferDesc& desc) const
  -> std::shared_ptr<graphics::Buffer>
{
  auto b = std::make_shared<Buffer>(desc);
  // Mark buffers created by the headless factory as initialized.
  b->MarkInitialized();
  return b;
}
OXGN_HDLS_NDAPI auto Graphics::CreateCommandQueue(std::string_view queue_name,
  QueueRole role, QueueAllocationPreference allocation_preference)
  -> std::shared_ptr<graphics::CommandQueue>
{
  // Delegate queue creation/lookup to the QueueManager component.
  auto& qm = GetComponent<internal::QueueManager>();
  return qm.CreateCommandQueue(queue_name, role, allocation_preference);
}
[[nodiscard]] auto Graphics::CreateSurface(
  std::weak_ptr<platform::Window> /*window_weak*/,
  std::shared_ptr<graphics::CommandQueue> /*command_queue*/) const
  -> std::shared_ptr<Surface>
{
  return std::make_shared<HeadlessSurface>("headless-surface");
}
[[nodiscard]] auto Graphics::GetShader(std::string_view unique_id) const
  -> std::shared_ptr<IShaderByteCode>
{
  auto& shaders = GetComponent<internal::EngineShaders>();
  return shaders.GetShader(unique_id);
}
OXGN_HDLS_NDAPI auto Graphics::CreateCommandListImpl(QueueRole role,
  std::string_view command_list_name) -> std::unique_ptr<CommandList>
{
  LOG_F(INFO, "Headless CreateCommandList requested: role={} name={}",
    nostd::to_string(role), command_list_name);
  const auto name = command_list_name.empty()
    ? std::string_view("headless-cmdlist")
    : command_list_name;
  return std::make_unique<CommandList>(name, role);
}
[[nodiscard]] auto Graphics::CreateRendererImpl(std::string_view /*name*/,
  std::weak_ptr<Surface> /*surface*/, frame::SlotCount /*frames_in_flight*/)
  -> std::unique_ptr<RenderController>
{
  return nullptr;
}

} // namespace oxygen::graphics::headless
