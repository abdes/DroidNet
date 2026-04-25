//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
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

namespace oxygen::vortex::upload::testing {

auto UploadCoordinatorTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;
  using internal::UploaderTagFactory;
  using vortex::testing::FakeGraphics;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());
  staging_provider_
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1 }, 4, 0.5f);
}

} // namespace oxygen::vortex::upload::testing
