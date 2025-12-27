//===----------------------------------------------------------------------===//
// Copyright (c) DroidNet
//
// This file is part of Oxygen.Engine.
//
// Oxygen.Engine is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// Oxygen.Engine is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Oxygen.Engine. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#if defined(OXYGEN_ENGINE_TESTING)

namespace oxygen::content::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::content::internal

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

auto MaterialBinderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<engine::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, engine::upload::DefaultUploadPolicy());

  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 1 }, 4);

  asset_loader_ = std::make_unique<content::AssetLoader>(
    content::internal::EngineTagFactory::Get());

  texture_binder_ = std::make_unique<resources::TextureBinder>(
    observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() },
    observer_ptr { asset_loader_.get() });

  material_binder_ = std::make_unique<resources::MaterialBinder>(
    observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() },
    observer_ptr { texture_binder_.get() });
}

auto MaterialBinderTest::GfxPtr() const -> observer_ptr<::oxygen::Graphics>
{
  return observer_ptr<::oxygen::Graphics>(gfx_.get());
}

auto MaterialBinderTest::Uploader() -> engine::upload::UploadCoordinator&
{
  return *uploader_;
}

auto MaterialBinderTest::AssetLoaderRef() -> content::AssetLoader&
{
  return *asset_loader_;
}

auto MaterialBinderTest::TextureBinderRef() -> resources::TextureBinder&
{
  return *texture_binder_;
}

auto MaterialBinderTest::MaterialBinderRef() -> resources::MaterialBinder&
{
  return *material_binder_;
}

auto MaterialBinderTest::AllocatedTextureSrvCount() const -> uint32_t
{
  const auto& allocator = gfx_->GetDescriptorAllocator();
  return allocator
    .GetAllocatedDescriptorsCount(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible)
    .get();
}

} // namespace oxygen::renderer::testing
