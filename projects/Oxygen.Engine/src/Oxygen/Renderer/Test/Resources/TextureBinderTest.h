//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

namespace oxygen::renderer::testing {

//! Test harness for TextureBinder unit tests.
/*! Provides a minimal renderer upload environment (FakeGraphics + real
    UploadCoordinator + staging provider) without depending on the
    UploadCoordinator test suite.

    The harness also owns an AssetLoader instance and constructs a TextureBinder
    ready for tests.
*/
class TextureBinderTest : public ::testing::Test {
protected:
  TextureBinderTest() = default;

  virtual auto ConfigureGraphics(renderer::testing::FakeGraphics&) -> void { }

  auto SetUp() -> void override;

  [[nodiscard]] auto Gfx() -> renderer::testing::FakeGraphics& { return *gfx_; }
  [[nodiscard]] auto GfxPtr() const -> observer_ptr<::oxygen::Graphics>
  {
    return observer_ptr<::oxygen::Graphics>(gfx_.get());
  }

  [[nodiscard]] auto Uploader() -> engine::upload::UploadCoordinator&
  {
    return *uploader_;
  }

  [[nodiscard]] auto Staging() -> engine::upload::StagingProvider&
  {
    return *staging_provider_;
  }

  [[nodiscard]] auto AssetLoaderRef() -> content::AssetLoader&
  {
    return *asset_loader_;
  }

  [[nodiscard]] auto Binder() -> resources::TextureBinder&
  {
    return *texture_binder_;
  }

  [[nodiscard]] auto AllocatedSrvCount() const -> uint32_t;

private:
  std::shared_ptr<renderer::testing::FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<content::AssetLoader> asset_loader_;
  std::unique_ptr<resources::TextureBinder> texture_binder_;
};

} // namespace oxygen::renderer::testing
