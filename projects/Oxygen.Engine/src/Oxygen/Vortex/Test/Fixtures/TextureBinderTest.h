//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::testing {

//! Test harness for TextureBinder unit tests.
/*! Provides a minimal renderer upload environment (FakeGraphics + real
    UploadCoordinator + staging provider) without depending on the
    UploadCoordinator test suite.

    The harness also owns a fake IAssetLoader and constructs a
    TextureBinder ready for tests.
*/
class TextureBinderTest : public ::testing::Test {
protected:
  TextureBinderTest() = default;

  virtual auto ConfigureGraphics(FakeGraphics& /*gfx*/) -> void { }

  auto SetUp() -> void override;

  [[nodiscard]] auto Gfx() const -> FakeGraphics& { return *gfx_; }
  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>
  {
    return observer_ptr<Graphics>(gfx_.get());
  }

  [[nodiscard]] auto Uploader() const -> vortex::upload::UploadCoordinator&
  {
    return *uploader_;
  }

  [[nodiscard]] auto Staging() const -> vortex::upload::StagingProvider&
  {
    return *staging_provider_;
  }

  [[nodiscard]] auto Loader() const -> FakeAssetLoader&
  {
    return *texture_loader_;
  }

  [[nodiscard]] auto TexBinder() const -> resources::TextureBinder&
  {
    return *texture_binder_;
  }

  [[nodiscard]] auto AllocatedSrvCount() const -> uint32_t;

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<vortex::upload::UploadCoordinator> uploader_;
  std::shared_ptr<vortex::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeAssetLoader> texture_loader_;
  std::unique_ptr<resources::TextureBinder> texture_binder_;
};

} // namespace oxygen::vortex::testing
