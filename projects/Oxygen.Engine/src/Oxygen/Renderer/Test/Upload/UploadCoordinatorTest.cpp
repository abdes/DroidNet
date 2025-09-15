//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

// Implementation of UploaderTagFactory. Provides access to UploaderTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if defined(OXYGEN_ENGINE_TESTING)
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
#endif

namespace oxygen::engine::upload::testing {

auto UploadCoordinatorTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;
  using internal::UploaderTagFactory;
  using renderer::testing::FakeGraphics;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());
  staging_provider_
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1 }, 4, 0.5f);
}

} // namespace oxygen::engine::upload::testing
