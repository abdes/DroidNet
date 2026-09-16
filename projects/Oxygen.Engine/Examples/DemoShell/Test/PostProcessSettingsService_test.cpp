//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <memory>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

#include "DemoShell/Services/PostProcessSettingsService.h"

namespace oxygen::examples::testing {
namespace {
  class PostProcessSettingsServiceTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      graphics_ = std::make_shared<vortex::testing::FakeGraphics>();
      graphics_->CreateCommandQueues(graphics::SingleQueueStrategy());
      auto config = RendererConfig {};
      config.upload_queue_key
        = graphics_->QueueKeyFor(graphics::QueueRole::kGraphics).get();
      renderer_ = std::make_unique<vortex::Renderer>(graphics_, config);
      service_.BindVortexRenderer(observer_ptr { renderer_.get() });
    }
    void TearDown() override
    {
      service_.BindVortexRenderer({});
      renderer_->OnShutdown();
    }
    auto Register(ViewId intent, std::uint64_t handle,
      ViewId source = kInvalidViewId) -> ViewId
    {
      auto view = engine::ViewContext {};
      view.metadata.exposure_view_id = source;
      return renderer_->UpsertPublishedRuntimeView(frame_, intent, view, {}, {},
        vortex::CompositionView::ViewStateHandle { handle });
    }
    std::shared_ptr<vortex::testing::FakeGraphics> graphics_;
    std::unique_ptr<vortex::Renderer> renderer_;
    engine::FrameContext frame_;
    ui::PostProcessSettingsService service_;
  };
} // namespace

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  ResetSeedsEachRegisteredOwnerAndSkipsBorrowers)
{
  const auto first = Register(ViewId { 1U }, 11U);
  Register(ViewId { 2U }, 22U);
  Register(ViewId { 3U }, 33U, first);
  service_.ResetAutoExposure(8.0F);
  for (const auto handle : { 11U, 22U }) {
    const auto status = renderer_->InspectExposureTransition(
      vortex::CompositionView::ViewStateHandle { handle });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->phase, vortex::ExposureTransitionPhase::kQueued);
    EXPECT_EQ(
      status->request.policy, vortex::ExposureTransitionPolicy::kSeedFromEv100);
    EXPECT_EQ(status->request.seed_ev, 8.0F);
  }
  EXPECT_FALSE(renderer_
      ->InspectExposureTransition(
        vortex::CompositionView::ViewStateHandle { 33U })
      .has_value());
  EXPECT_EQ(renderer_->GetExposureOwners(),
    (std::vector { vortex::CompositionView::ViewStateHandle { 11U },
      vortex::CompositionView::ViewStateHandle { 22U } }));
}

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  RepeatedResetUsesNewRendererGenerationsAndInvalidSeedDoesNotReplaceIntent)
{
  Register(ViewId { 1U }, 11U);
  const auto handle = vortex::CompositionView::ViewStateHandle { 11U };
  service_.ResetAutoExposure(4.0F);
  const auto first = renderer_->InspectExposureTransition(handle)->request;
  service_.ResetAutoExposure(8.0F);
  const auto second = renderer_->InspectExposureTransition(handle)->request;
  EXPECT_GT(second.generation, first.generation);
  EXPECT_EQ(renderer_->RetryExposureTransition(first),
    vortex::ExposureTransitionPhase::kSuperseded);
  service_.ResetAutoExposure(std::numeric_limits<float>::infinity());
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, second);
  service_.BindVortexRenderer({});
  service_.ResetAutoExposure(12.0F);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, second);
}

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  ResetDoesNotPersistIntentForFutureOrRetiredViews)
{
  service_.ResetAutoExposure(8.0F);
  Register(ViewId { 1U }, 11U);
  const auto handle = vortex::CompositionView::ViewStateHandle { 11U };
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
  renderer_->RemovePublishedRuntimeView(frame_, ViewId { 1U });
  service_.ResetAutoExposure(8.0F);
  EXPECT_TRUE(renderer_->GetExposureOwners().empty());
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
}
} // namespace oxygen::examples::testing
