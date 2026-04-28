//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>
#include <stdexcept>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Renderer.h>

#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ViewId;
using oxygen::engine::FrameContext;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::RendererPublicationProbe;

class RuntimeViewPublicationTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    renderer_ = std::make_unique<Renderer>(
      std::weak_ptr<Graphics>(graphics_), std::move(config));
  }

  void TearDown() override
  {
    if (renderer_) {
      renderer_->OnShutdown();
    }
  }

  [[nodiscard]] static auto MakeViewContext(std::string_view name)
    -> oxygen::engine::ViewContext
  {
    oxygen::engine::ViewContext view {};
    view.view.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view.metadata.name = std::string(name);
    view.metadata.purpose = "runtime";
    return view;
  }

  static auto PrepareFrameContext(
    FrameContext& frame_context, const std::uint32_t frame_number) -> void
  {
    frame_context.SetCurrentPhase(oxygen::core::PhaseId::kFrameStart,
      oxygen::engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSequenceNumber(
      oxygen::frame::SequenceNumber { frame_number },
      oxygen::engine::internal::EngineTagFactory::Get());
  }

  [[nodiscard]] auto MakeFramebuffer() const -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = 64U;
    color_desc.height = 64U;
    color_desc.format = oxygen::Format::kRGBA8UNorm;
    color_desc.texture_type = oxygen::TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = "RuntimeViewPublicationTest.Color";

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MaterializeViewConstantsBuffer(const ViewId view_id) const
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    if (!framebuffer_) {
      framebuffer_ = MakeFramebuffer();
    }

    auto params = oxygen::ResolvedView::Params {};
    params.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };

    auto result = renderer_->ForSinglePassHarness()
                    .SetFrameSession(Renderer::FrameSessionInput {
                      .frame_slot = oxygen::frame::Slot { 0U },
                      .frame_sequence = oxygen::frame::SequenceNumber { 1U },
                      .delta_time_seconds
                      = oxygen::time::SimulationClock::kMinDeltaTimeSeconds,
                    })
                    .SetOutputTarget(Renderer::OutputTargetInput {
                      .framebuffer = oxygen::observer_ptr<Framebuffer>(
                        framebuffer_.get()),
                    })
                    .SetResolvedView(Renderer::ResolvedViewInput {
                      .view_id = view_id,
                      .value = oxygen::ResolvedView(params),
                    })
                    .SetPreparedFrame(Renderer::PreparedFrameInput {})
                    .Finalize();

    EXPECT_TRUE(result.has_value());
    if (!result.has_value()) {
      return {};
    }

    return result->GetRenderContext().view_constants;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  mutable std::shared_ptr<Framebuffer> framebuffer_;
  std::unique_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(RuntimeViewPublicationTest,
  UpsertPublishedRuntimeViewRegistersAndUpdatesStablePublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 11U };
  const auto first_published = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("first"));
  EXPECT_NE(first_published, oxygen::kInvalidViewId);
  EXPECT_EQ(
    renderer_->ResolvePublishedRuntimeViewId(intent_view_id), first_published);

  const auto second_published = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("updated"));
  EXPECT_EQ(second_published, first_published);
  EXPECT_EQ(
    frame_context.GetViewContext(first_published).metadata.name, "updated");
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewClearsMappingAndFrameContextView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 12U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("removable"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  renderer_->RemovePublishedRuntimeView(frame_context, intent_view_id);

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_THROW(
    static_cast<void>(frame_context.GetViewContext(published_view_id)),
    std::out_of_range);
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, PruneStalePublishedRuntimeViewsEvictsOldMappings)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 13U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("stale"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  frame_context.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1000U },
    oxygen::engine::internal::EngineTagFactory::Get());
  const auto pruned = renderer_->PruneStalePublishedRuntimeViews(frame_context);

  ASSERT_EQ(pruned.size(), 1U);
  EXPECT_EQ(pruned.front(), intent_view_id);
  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_THROW(
    static_cast<void>(frame_context.GetViewContext(published_view_id)),
    std::out_of_range);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewReleasesTrackedViewConstantsForPublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 14U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("removable-view-constants"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  renderer_->RemovePublishedRuntimeView(frame_context, intent_view_id);

  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewWithoutFrameContextReleasesTrackedViewConstants)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 16U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("shutdown-removable"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  renderer_->RemovePublishedRuntimeView(intent_view_id);

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  PruneStalePublishedRuntimeViewsReleasesTrackedViewConstantsForPublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 15U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("stale-view-constants"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  frame_context.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1000U },
    oxygen::engine::internal::EngineTagFactory::Get());
  const auto pruned = renderer_->PruneStalePublishedRuntimeViews(frame_context);

  ASSERT_EQ(pruned.size(), 1U);
  EXPECT_EQ(pruned.front(), intent_view_id);

  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  OnShutdownResetsViewConstantsManagerAfterRuntimeViewCleanup)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 17U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("shutdown-cleanup"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);
  ASSERT_NE(MaterializeViewConstantsBuffer(published_view_id), nullptr);

  renderer_->OnShutdown();

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_EQ(RendererPublicationProbe::GetViewConstantsManager(*renderer_),
    nullptr);
}

} // namespace
