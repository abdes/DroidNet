//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Fixtures/TextureBinderTest.h>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

namespace oxygen::vortex::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace oxygen::vortex::testing {

auto TextureBinderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  ConfigureGraphics(*gfx_);
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<vortex::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, vortex::upload::DefaultUploadPolicy());

  constexpr float kSlack = 0.5F;
  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 1 }, 4, kSlack);

  texture_loader_ = std::make_unique<FakeAssetLoader>();

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

} // namespace oxygen::vortex::testing
