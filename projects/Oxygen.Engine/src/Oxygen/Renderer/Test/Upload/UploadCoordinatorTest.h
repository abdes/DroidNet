//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

namespace oxygen::engine::upload::testing {

class UploadCoordinatorTest : public ::testing::Test {
protected:
  UploadCoordinatorTest() = default;

  auto SetUp() -> void override;

  auto TearDown() -> void override { }

  auto GfxPtr() const { return observer_ptr { gfx_.get() }; }

  auto Uploader(UploadPolicy policy = DefaultUploadPolicy())
    -> UploadCoordinator&
  {
    uploader_ = std::make_unique<UploadCoordinator>(GfxPtr(), policy);
    return *uploader_;
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  auto Staging() -> StagingProvider& { return *staging_provider_; }

  auto SimulateFrameStart(frame::Slot slot) -> void
  {
    // Simulate frame advance to complete fences
    uploader_->OnFrameStart(
      renderer::internal::RendererTagFactory::Get(), slot);
  }

private:
  std::shared_ptr<renderer::testing::FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
};

} // namespace oxygen::engine::upload::testing
