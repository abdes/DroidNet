//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>

#include "Oxygen/Platform/Input.h"

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#ifdef OXYGEN_ENGINE_TESTING

namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

#endif // OXYGEN_ENGINE_TESTING

namespace oxygen::renderer::testing {

auto TextureBinderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  ConfigureGraphics(*gfx_);
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<engine::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, engine::upload::DefaultUploadPolicy());

  constexpr float kSlack = 0.5F;
  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 1 }, 4, kSlack);

  texture_loader_ = std::make_unique<FakeTextureResourceLoader>();

  texture_binder_ = std::make_unique<resources::TextureBinder>(
    observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
    observer_ptr { uploader_.get() }, observer_ptr { texture_loader_.get() });
}

auto TextureBinderTest::AllocatedSrvCount() const -> uint32_t
{
  const graphics::DescriptorAllocator& allocator
    = gfx_->GetDescriptorAllocator();
  return allocator
    .GetAllocatedDescriptorsCount(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible)
    .get();
}

} // namespace oxygen::renderer::testing
