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

#pragma once

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Testing/GTest.h>

#include <memory>

namespace oxygen::renderer::testing {

class MaterialBinderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<::oxygen::Graphics>;

  [[nodiscard]] auto Uploader() -> engine::upload::UploadCoordinator&;
  [[nodiscard]] auto AssetLoaderRef() -> content::AssetLoader&;
  [[nodiscard]] auto TextureBinderRef() -> resources::TextureBinder&;
  [[nodiscard]] auto MaterialBinderRef() -> resources::MaterialBinder&;

  [[nodiscard]] auto AllocatedTextureSrvCount() const -> uint32_t;

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<content::AssetLoader> asset_loader_;
  std::unique_ptr<resources::TextureBinder> texture_binder_;
  std::unique_ptr<resources::MaterialBinder> material_binder_;
};

} // namespace oxygen::renderer::testing
