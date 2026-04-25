//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/FacadePresets.h>
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
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;

class FacadePresetsTest : public ::testing::Test {
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
    color_desc.debug_name = "FacadePresetsTest.Color";

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
      .view_id = ViewId { 91U },
      .value = oxygen::ResolvedView(params),
    };
  }

  [[nodiscard]] static auto MakePreparedFrameInput()
    -> Renderer::PreparedFrameInput
  {
    return Renderer::PreparedFrameInput {
      .value = oxygen::vortex::PreparedSceneFrame {},
    };
  }

  [[nodiscard]] auto AcquireRecorder(std::string_view name) const
  {
    return graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(QueueRole::kGraphics), name, false);
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Framebuffer> framebuffer_;
  std::unique_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(
  FacadePresetsTest, FullscreenPresetFinalizesWithOnlyCoreShaderInputs)
{
  auto facade
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput { .frame_slot = oxygen::frame::Slot { 0U } },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      ViewId { 5U });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetRenderContext().current_view.view_id, ViewId { 5U });
}

NOLINT_TEST_F(FacadePresetsTest,
  PreparedScenePresetFinalizesFromResolvedViewAndPreparedFrame)
{
  auto facade = oxygen::vortex::harness::single_pass::presets::
    ForPreparedSceneGraphicsPass(*renderer_,
      Renderer::FrameSessionInput { .frame_slot = oxygen::frame::Slot { 0U } },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(), MakePreparedFrameInput());

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetRenderContext().current_view.view_id, ViewId { 91U });
}

NOLINT_TEST_F(FacadePresetsTest, RenderGraphPresetFinalizesWithCallerGraph)
{
  auto facade
    = oxygen::vortex::harness::render_graph::presets::ForSingleViewGraph(
      *renderer_,
      Renderer::FrameSessionInput { .frame_slot = oxygen::frame::Slot { 0U } },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(),
      [](ViewId, const oxygen::vortex::RenderContext&,
        oxygen::graphics::CommandRecorder&) -> oxygen::co::Co<void> {
        co_return;
      });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetViewId(), ViewId { 91U });
}

NOLINT_TEST_F(FacadePresetsTest,
  PreparedScenePresetCarriesExplicitCoreInputsOnMigratedSubstrate)
{
  auto facade = oxygen::vortex::harness::single_pass::presets::
    ForPreparedSceneGraphicsPass(*renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 2U },
        .frame_sequence = oxygen::frame::SequenceNumber { 19U },
      },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(), MakePreparedFrameInput(),
      Renderer::CoreShaderInputsInput {
        .view_id = ViewId { 91U },
        .value = oxygen::vortex::ViewConstants {},
      });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 91U });
  EXPECT_NE(render_context.current_view.prepared_frame.get(), nullptr);
  EXPECT_NE(render_context.view_constants.get(), nullptr);
}

NOLINT_TEST_F(FacadePresetsTest,
  RenderGraphPresetExecutesAgainstPreparedSceneSubstrate)
{
  auto executed = false;
  auto facade = oxygen::vortex::harness::render_graph::presets::
    ForSingleViewGraph(*renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 2U },
        .frame_sequence = oxygen::frame::SequenceNumber { 23U },
      },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(), MakePreparedFrameInput(),
      Renderer::CoreShaderInputsInput {
        .view_id = ViewId { 91U },
        .value = oxygen::vortex::ViewConstants {},
      },
      [&executed](ViewId view_id, const RenderContext& context,
        oxygen::graphics::CommandRecorder&) -> oxygen::co::Co<void> {
        executed = true;
        EXPECT_EQ(view_id, ViewId { 91U });
        EXPECT_EQ(context.current_view.view_id, ViewId { 91U });
        EXPECT_NE(context.current_view.prepared_frame.get(), nullptr);
        EXPECT_NE(context.view_constants.get(), nullptr);
        co_return;
      });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());

  auto recorder = AcquireRecorder("FacadePresetsTest.PreparedSceneGraph");
  ASSERT_NE(recorder, nullptr);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop,
    [&]() -> oxygen::co::Co<void> { co_await result->Execute(*recorder); });

  EXPECT_TRUE(executed);
}

NOLINT_TEST_F(FacadePresetsTest,
  PreparedScenePresetRemainsValidWithoutEnvironmentBridgeInputs)
{
  auto facade = oxygen::vortex::harness::single_pass::presets::
    ForPreparedSceneGraphicsPass(*renderer_,
      Renderer::FrameSessionInput { .frame_slot = oxygen::frame::Slot { 0U } },
      oxygen::observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(), MakePreparedFrameInput());

  auto validation = facade.Validate();
  EXPECT_TRUE(validation.Ok());

  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetRenderContext().current_view.composition_view.get(), nullptr);
}

} // namespace
