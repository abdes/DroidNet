//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/SingleBufferStaging.h>
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
#endif

namespace oxygen::engine::upload::testing {

auto UploadCoordinatorTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;
  using internal::UploaderTagFactory;
  using renderer::testing::FakeGraphics;
  using upload::SingleBufferStaging;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());
  staging_provider_ = std::make_shared<SingleBufferStaging>(
    UploaderTagFactory::Get(), GfxPtr());
}

} // namespace oxygen::engine::upload::testing
