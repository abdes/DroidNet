//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Internal/RenderContextMaterializer.h>
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
using oxygen::vortex::internal::BasicRenderContextMaterializer;
using oxygen::vortex::testing::FakeGraphics;

using FrameSessionInput = Renderer::FrameSessionInput;
using OutputTargetInput = Renderer::OutputTargetInput;
using ResolvedViewInput = Renderer::ResolvedViewInput;
using PreparedFrameInput = Renderer::PreparedFrameInput;
using CoreShaderInputsInput = Renderer::CoreShaderInputsInput;
using RenderContextMaterializer = BasicRenderContextMaterializer<Renderer>;
using SinglePassHarnessStaging
  = RenderContextMaterializer::SinglePassHarnessStaging;

class RenderContextMaterializerTest : public ::testing::Test {
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
    color_desc.debug_name = "RenderContextMaterializerTest.Color";

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeResolvedViewInput() const -> ResolvedViewInput
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
    return ResolvedViewInput {
      .view_id = ViewId { 41U },
      .value = oxygen::ResolvedView(params),
    };
  }

  [[nodiscard]] auto MakeValidStaging() const -> SinglePassHarnessStaging
  {
    if (!framebuffer_) {
      framebuffer_ = MakeFramebuffer();
    }

    return SinglePassHarnessStaging {
      .frame_session = FrameSessionInput { .frame_slot = oxygen::frame::Slot { 0U } },
      .output_target
      = OutputTargetInput {
          .framebuffer = oxygen::observer_ptr<const Framebuffer>(framebuffer_.get()),
        },
      .resolved_view = MakeResolvedViewInput(),
      .prepared_frame = PreparedFrameInput {},
      .core_shader_inputs = std::nullopt,
    };
  }

  std::shared_ptr<FakeGraphics> graphics_;
  mutable std::shared_ptr<Framebuffer> framebuffer_;
  std::unique_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(
  RenderContextMaterializerTest, ValidateSinglePassRequiresFrameSession)
{
  auto staging = MakeValidStaging();
  staging.frame_session.reset();

  auto materializer = RenderContextMaterializer(*renderer_);
  const auto report = materializer.ValidateSinglePass(staging);

  EXPECT_FALSE(report.Ok());
  ASSERT_FALSE(report.issues.empty());
  EXPECT_EQ(report.issues.front().code, "frame_session.missing");
}

NOLINT_TEST_F(
  RenderContextMaterializerTest, ValidateSinglePassRequiresOutputTarget)
{
  auto staging = MakeValidStaging();
  staging.output_target.reset();

  auto materializer = RenderContextMaterializer(*renderer_);
  const auto report = materializer.ValidateSinglePass(staging);

  EXPECT_FALSE(report.Ok());
  ASSERT_FALSE(report.issues.empty());
  EXPECT_EQ(report.issues.front().code, "output_target.missing");
}

NOLINT_TEST_F(RenderContextMaterializerTest,
  ValidateSinglePassRequiresSatisfiableCoreShaderInputs)
{
  auto staging = MakeValidStaging();
  staging.resolved_view.reset();
  staging.core_shader_inputs.reset();

  auto materializer = RenderContextMaterializer(*renderer_);
  const auto report = materializer.ValidateSinglePass(staging);

  EXPECT_FALSE(report.Ok());
  ASSERT_FALSE(report.issues.empty());
  EXPECT_EQ(report.issues.front().code, "core_shader_inputs.unsatisfied");
}

NOLINT_TEST_F(RenderContextMaterializerTest,
  MaterializeSinglePassBindsFrameStateAndOutputTarget)
{
  auto staging = MakeValidStaging();
  staging.frame_session = FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 2U },
    .frame_sequence = oxygen::frame::SequenceNumber { 9U },
    .delta_time_seconds = 1.0F / 30.0F,
  };

  auto materializer = RenderContextMaterializer(*renderer_);
  auto result = materializer.MaterializeSinglePass(staging);

  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  ASSERT_NE(render_context.pass_target.get(), nullptr);
  EXPECT_EQ(render_context.frame_slot, oxygen::frame::Slot { 2U });
  EXPECT_EQ(
    render_context.frame_sequence, oxygen::frame::SequenceNumber { 9U });
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 41U });
  EXPECT_NE(render_context.current_view.resolved_view.get(), nullptr);
  EXPECT_NE(render_context.current_view.prepared_frame.get(), nullptr);
}

NOLINT_TEST_F(RenderContextMaterializerTest,
  MaterializeSinglePassAcceptsExplicitCoreShaderInputsWithoutResolvedView)
{
  auto staging = MakeValidStaging();
  staging.resolved_view.reset();
  staging.core_shader_inputs = CoreShaderInputsInput {
    .view_id = ViewId { 77U },
    .value = oxygen::vortex::ViewConstants {},
  };

  auto materializer = RenderContextMaterializer(*renderer_);
  auto result = materializer.MaterializeSinglePass(staging);

  ASSERT_TRUE(result.has_value());
  const auto& render_context = result->GetRenderContext();
  EXPECT_EQ(render_context.current_view.view_id, ViewId { 77U });
  EXPECT_NE(render_context.view_constants.get(), nullptr);
}

} // namespace
