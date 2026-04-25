//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>
#include <string_view>
#include <type_traits>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::PostProcessConfig;
using oxygen::vortex::PostProcessFrameBindings;
using oxygen::vortex::PostProcessService;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key = graphics->QueueKeyFor(QueueRole::kGraphics).get();
  constexpr auto kCapabilities = RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kFinalOutputComposition;
  return { new Renderer(std::weak_ptr<Graphics>(graphics), std::move(config),
             kCapabilities),
    DestroyRenderer };
}

auto MakeFramebuffer(const std::shared_ptr<FakeGraphics>& graphics,
  std::string_view debug_name) -> std::shared_ptr<Framebuffer>
{
  auto color_desc = TextureDesc {};
  color_desc.width = 64U;
  color_desc.height = 64U;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = ResourceStates::kCommon;
  color_desc.debug_name = std::string(debug_name);

  auto color = graphics->CreateTexture(color_desc);
  auto fb_desc = FramebufferDesc {};
  fb_desc.AddColorAttachment({ .texture = color });
  return graphics->CreateFramebuffer(fb_desc);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessFrameBindingsExposeStage22AuthoritySurface)
{
  const auto bindings = PostProcessFrameBindings {};

  EXPECT_EQ(bindings.resolved_scene_color_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_depth_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_velocity_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.bloom_texture_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_uav, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.post_history_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.fixed_exposure, 1.0F);
  EXPECT_EQ(bindings.enable_bloom, 1U);
  EXPECT_EQ(bindings.enable_auto_exposure, 1U);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessConfigDefaultsRemainTonemapFirstAndFixedExposureSafe)
{
  const auto config = PostProcessConfig {};

  EXPECT_TRUE(config.enable_bloom);
  EXPECT_TRUE(config.enable_auto_exposure);
  EXPECT_FLOAT_EQ(config.fixed_exposure, 1.0F);
  EXPECT_FLOAT_EQ(config.bloom_intensity, 0.5F);
  EXPECT_FLOAT_EQ(config.bloom_threshold, 1.0F);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<PostProcessService>));
  EXPECT_TRUE((std::is_destructible_v<PostProcessService>));
  EXPECT_TRUE((std::is_standard_layout_v<PostProcessFrameBindings>));
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  FixedExposureAndAutoEvClampShareTheSameEv100CalibrationBasis)
{
  constexpr float kEv100 = 14.0F;
  constexpr float kCompensationEv = 1.25F;
  constexpr float kExposureKey = 10.0F;

  const float manual_scale = oxygen::engine::ExposureScaleFromEv100(
    kEv100, kCompensationEv, kExposureKey);
  const float auto_target_luminance = oxygen::engine::kExposureMiddleGrey
    * oxygen::engine::ExposureBiasScale(kCompensationEv, kExposureKey);
  const float average_luminance
    = oxygen::engine::Ev100ToAverageLuminance(kEv100);
  const float auto_scale = auto_target_luminance / average_luminance;

  EXPECT_FLOAT_EQ(
    oxygen::engine::AverageLuminanceToEv100(average_luminance), kEv100);
  EXPECT_FLOAT_EQ(manual_scale, auto_scale);
}

class PostProcessServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::shared_ptr<Renderer> renderer_ {};
};

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExecutePublishesStage22BindingsAndRecordsTonemapVisibleOutput)
{
  auto service = PostProcessService(*renderer_);
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.Output");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 41U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 9U };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  service.Execute(context.current_view.view_id, context, scene_textures,
    PostProcessService::Inputs {
      .scene_signal = &scene_textures.GetSceneColor(),
      .scene_depth = &scene_textures.GetSceneDepth(),
      .scene_velocity = scene_textures.GetVelocity(),
      .post_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
      .scene_signal_srv = oxygen::ShaderVisibleIndex { 301U },
      .scene_depth_srv = oxygen::ShaderVisibleIndex { 302U },
      .scene_velocity_srv = oxygen::ShaderVisibleIndex { 303U },
    });

  const auto& state = service.GetLastExecutionState();
  ASSERT_NE(service.InspectBindings(context.current_view.view_id), nullptr);
  EXPECT_TRUE(state.published_bindings);
  EXPECT_NE(state.post_process_frame_slot, kInvalidShaderVisibleIndex);
  EXPECT_TRUE(state.tonemap_requested);
  EXPECT_TRUE(state.tonemap_executed);
  EXPECT_TRUE(state.wrote_visible_output);
  EXPECT_EQ(service.ResolveBindingSlot(context.current_view.view_id),
    state.post_process_frame_slot);
  EXPECT_EQ(service.InspectBindings(context.current_view.view_id)
              ->resolved_scene_color_srv,
    oxygen::ShaderVisibleIndex { 301U });
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->graphics_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.PostProcess.Tonemap";
    }));
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExecutePublishesConfiguredAutoExposureEvClampBindings)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.auto_exposure_min_ev = -3.5F;
  config.auto_exposure_max_ev = 11.25F;
  service.SetConfig(config);

  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.ClampOutput");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 42U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 10U };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  service.Execute(context.current_view.view_id, context, scene_textures,
    PostProcessService::Inputs {
      .scene_signal = &scene_textures.GetSceneColor(),
      .scene_depth = &scene_textures.GetSceneDepth(),
      .scene_velocity = scene_textures.GetVelocity(),
      .post_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
      .scene_signal_srv = oxygen::ShaderVisibleIndex { 401U },
      .scene_depth_srv = oxygen::ShaderVisibleIndex { 402U },
      .scene_velocity_srv = oxygen::ShaderVisibleIndex { 403U },
    });

  const auto* bindings = service.InspectBindings(context.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_min_ev, config.auto_exposure_min_ev);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_max_ev, config.auto_exposure_max_ev);
}

} // namespace
