//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;

class SinglePassHarnessFacadeTest : public ::testing::Test {
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
    framebuffer_ = MakeFramebuffer();
  }

  void TearDown() override
  {
    if (renderer_) {
      renderer_->OnShutdown();
    }
  }

  [[nodiscard]] auto MakeFramebuffer() const -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = 64U;
    color_desc.height = 64U;
    color_desc.format = Format::kRGBA8UNorm;
    color_desc.texture_type = TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = "SinglePassHarnessFacadeTest.Color";

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeResolvedViewInput() const
    -> Renderer::ResolvedViewInput
  {
    auto params = oxygen::ResolvedView::Params {};
    params.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    return Renderer::ResolvedViewInput {
      .view_id = ViewId { 51U },
      .value = oxygen::ResolvedView(params),
    };
  }

  [[nodiscard]] auto MakeOutputTarget() const -> Renderer::OutputTargetInput
  {
    return Renderer::OutputTargetInput {
      .framebuffer
      = oxygen::observer_ptr<const Framebuffer>(framebuffer_.get()),
    };
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
  std::unique_ptr<Renderer> renderer_ {};
};

NOLINT_TEST_F(SinglePassHarnessFacadeTest, CanFinalizeTracksRequiredInputs)
{
  auto facade = renderer_->ForSinglePassHarness();

  EXPECT_FALSE(facade.CanFinalize());

  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  EXPECT_FALSE(facade.CanFinalize());

  facade.SetOutputTarget(MakeOutputTarget());
  EXPECT_FALSE(facade.CanFinalize());

  facade.SetResolvedView(MakeResolvedViewInput());
  EXPECT_TRUE(facade.CanFinalize());
}

NOLINT_TEST_F(SinglePassHarnessFacadeTest, ValidateReportsMissingRequirements)
{
  auto facade = renderer_->ForSinglePassHarness();
  const auto report = facade.Validate();

  EXPECT_FALSE(report.Ok());
  EXPECT_GE(report.issues.size(), 3U);
}

NOLINT_TEST_F(SinglePassHarnessFacadeTest,
  FinalizeProducesExecutableContextFromResolvedView)
{
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 1U },
    .frame_sequence = oxygen::frame::SequenceNumber { 17U },
    .delta_time_seconds = 1.0F / 120.0F,
  });
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetResolvedView(MakeResolvedViewInput());

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  ASSERT_NE(render_context.pass_target.get(), nullptr);
  EXPECT_EQ(render_context.frame_slot, oxygen::frame::Slot { 1U });
  EXPECT_EQ(
    render_context.frame_sequence, oxygen::frame::SequenceNumber { 17U });
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 51U });
  EXPECT_NE(render_context.current_view.resolved_view.get(), nullptr);
}

NOLINT_TEST_F(SinglePassHarnessFacadeTest,
  FinalizeAcceptsExplicitCoreShaderInputsWithoutResolvedView)
{
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetCoreShaderInputs(Renderer::CoreShaderInputsInput {
    .view_id = ViewId { 77U },
    .value = oxygen::vortex::ViewConstants {},
  });

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 77U });
  EXPECT_NE(render_context.view_constants.get(), nullptr);
}

NOLINT_TEST_F(SinglePassHarnessFacadeTest,
  FinalizeKeepsARenderablePassTargetForStage22Output)
{
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 2U },
  });
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetResolvedView(MakeResolvedViewInput());

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  const auto* pass_target = result->GetRenderContext().pass_target.get();
  ASSERT_NE(pass_target, nullptr);
  ASSERT_EQ(pass_target->GetDescriptor().color_attachments.size(), 1U);
  ASSERT_NE(pass_target->GetDescriptor().color_attachments.front().texture, nullptr);
  EXPECT_TRUE(pass_target->GetDescriptor().color_attachments.front()
                .texture->GetDescriptor()
                .is_render_target);
}

NOLINT_TEST_F(SinglePassHarnessFacadeTest,
  FinalizeCarriesPreparedFrameAndExplicitCoreInputsOnMigratedSubstrate)
{
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 2U },
    .frame_sequence = oxygen::frame::SequenceNumber { 21U },
  });
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetResolvedView(MakeResolvedViewInput());
  facade.SetPreparedFrame(
    Renderer::PreparedFrameInput {
      .value = oxygen::vortex::PreparedSceneFrame {},
    });
  facade.SetCoreShaderInputs(Renderer::CoreShaderInputsInput {
    .view_id = ViewId { 51U },
    .value = oxygen::vortex::ViewConstants {},
  });

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  EXPECT_EQ(render_context.frame_slot, oxygen::frame::Slot { 2U });
  EXPECT_EQ(
    render_context.frame_sequence, oxygen::frame::SequenceNumber { 21U });
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 51U });
  EXPECT_NE(render_context.current_view.resolved_view.get(), nullptr);
  EXPECT_NE(render_context.current_view.prepared_frame.get(), nullptr);
  EXPECT_NE(render_context.view_constants.get(), nullptr);
}

} // namespace
