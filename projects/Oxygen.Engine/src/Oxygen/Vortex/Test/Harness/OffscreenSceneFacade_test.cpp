//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <ranges>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/FacadePresets.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/ViewFeatureProfile.h>

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::scene::PerspectiveCamera;
using oxygen::scene::Scene;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::CompositionView;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

class OffscreenSceneFacadeTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    const auto capabilities = CapabilitySet {
      RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition
    };
    renderer_ = { new Renderer(
                    std::weak_ptr<Graphics>(graphics_), std::move(config),
                    capabilities),
      DestroyRenderer };
    framebuffer_ = MakeFramebuffer();
    scene_ = std::make_shared<Scene>("OffscreenSceneFacadeTest", 16U);
    camera_ = MakeCameraNode(*scene_);
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
    color_desc.debug_name = "OffscreenSceneFacadeTest.Color";

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] static auto MakeCameraNode(Scene& scene)
    -> oxygen::scene::SceneNode
  {
    auto node = scene.CreateNode("OffscreenCamera");
    auto camera = std::make_unique<PerspectiveCamera>();
    camera->SetNearPlane(0.1F);
    camera->SetFarPlane(100.0F);
    camera->SetViewport(MakeView().viewport);
    EXPECT_TRUE(node.AttachCamera(std::move(camera)));
    scene.Update();
    return node;
  }

  [[nodiscard]] static auto MakeView() -> View
  {
    auto view = View {};
    view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    return view;
  }

  [[nodiscard]] auto MakeFrameSession() const -> Renderer::FrameSessionInput
  {
    return Renderer::FrameSessionInput {
      .frame_slot = oxygen::frame::Slot { 1U },
      .frame_sequence = oxygen::frame::SequenceNumber { 17U },
      .delta_time_seconds = 1.0F / 60.0F,
      .scene = oxygen::observer_ptr<Scene> { scene_.get() },
    };
  }

  [[nodiscard]] auto MakeOutputTarget() const -> Renderer::OutputTargetInput
  {
    return Renderer::OutputTargetInput {
      .framebuffer = oxygen::observer_ptr<Framebuffer> { framebuffer_.get() },
    };
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
  std::shared_ptr<Scene> scene_ {};
  oxygen::scene::SceneNode camera_ {};
  std::shared_ptr<Renderer> renderer_ {};
};

NOLINT_TEST_F(OffscreenSceneFacadeTest, ValidateRejectsInvalidViewId)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "InvalidOffscreen", oxygen::kInvalidViewId, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) {
    return issue.code == "view_intent.invalid_id";
  }));
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ValidateRejectsFramebufferWithoutColor)
{
  auto depth_desc = TextureDesc {};
  depth_desc.width = 64U;
  depth_desc.height = 64U;
  depth_desc.format = Format::kDepth32;
  depth_desc.texture_type = TextureType::kTexture2D;
  depth_desc.is_render_target = true;
  depth_desc.is_shader_resource = true;
  depth_desc.is_typeless = true;
  depth_desc.initial_state = ResourceStates::kCommon;
  depth_desc.debug_name = "OffscreenSceneFacadeTest.DepthOnly";

  auto fb_desc = FramebufferDesc {};
  fb_desc.SetDepthAttachment(graphics_->CreateTexture(depth_desc));
  auto framebuffer_without_color = graphics_->CreateFramebuffer(fb_desc);

  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "NoColorTarget", ViewId { 46U }, MakeView(), camera_));
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer
    = oxygen::observer_ptr<Framebuffer> { framebuffer_without_color.get() },
  });

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) {
    return issue.code == "output_target.invalid_framebuffer";
  }));
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PresetsFinalizeWithRenderableViewIds)
{
  auto preview = oxygen::vortex::offscreen::scene::presets::ForPreview(
    *renderer_, MakeFrameSession(), oxygen::observer_ptr<Scene> { scene_.get() },
    camera_, oxygen::observer_ptr<Framebuffer> { framebuffer_.get() });
  auto capture = oxygen::vortex::offscreen::scene::presets::ForCapture(
    *renderer_, MakeFrameSession(), oxygen::observer_ptr<Scene> { scene_.get() },
    camera_, oxygen::observer_ptr<Framebuffer> { framebuffer_.get() });

  auto preview_session = preview.Finalize();
  auto capture_session = capture.Finalize();

  ASSERT_TRUE(preview_session.has_value());
  ASSERT_TRUE(capture_session.has_value());
  EXPECT_NE(preview_session->GetViewId(), oxygen::kInvalidViewId);
  EXPECT_NE(capture_session->GetViewId(), oxygen::kInvalidViewId);
  EXPECT_NE(preview_session->GetViewId(), capture_session->GetViewId());
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineDefaultsToDeferred)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "DefaultPipeline", ViewId { 43U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  auto session = facade.Finalize();

  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->GetPipelineShadingMode(), ShadingMode::kDeferred);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineCanSelectForward)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "ForwardPipeline", ViewId { 44U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());

  auto session = facade.Finalize();

  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->GetPipelineShadingMode(), ShadingMode::kForward);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineCarriesFeatureProfile)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "NoEnvironmentPipeline", ViewId { 47U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput {
    .feature_profile = CompositionView::ViewFeatureProfile::kNoEnvironment,
  });

  auto session = facade.Finalize();

  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->GetPipelineFeatureProfile(),
    CompositionView::ViewFeatureProfile::kNoEnvironment);
}

NOLINT_TEST_F(
  OffscreenSceneFacadeTest, ValidateRejectsMissingProfileCapabilities)
{
  auto limited_config = RendererConfig {};
  limited_config.upload_queue_key
    = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
  auto limited_renderer = std::shared_ptr<Renderer>(
    new Renderer(std::weak_ptr<Graphics>(graphics_), std::move(limited_config),
      RendererCapabilityFamily::kScenePreparation
        | RendererCapabilityFamily::kDeferredShading
        | RendererCapabilityFamily::kLightingData
        | RendererCapabilityFamily::kFinalOutputComposition),
    DestroyRenderer);

  auto facade = limited_renderer->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "ShadowOnlyPipeline", ViewId { 48U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::ShadowOnly());

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) {
    return issue.code == "pipeline.missing_required_capabilities";
  }));
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ExecuteRendersIntoOutputTarget)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "OffscreenExecute", ViewId { 42U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  auto session = facade.Finalize();
  ASSERT_TRUE(session.has_value());

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(
    loop, [&]() -> oxygen::co::Co<void> { co_await session->Execute(); });

  EXPECT_FALSE(graphics_->draw_log_.draws.empty());
  const auto& color_texture
    = *framebuffer_->GetDescriptor().color_attachments.front().texture;
  auto queue = graphics_->GetCommandQueue(QueueRole::kGraphics);
  ASSERT_NE(queue.get(), nullptr);
  const auto final_state
    = queue->TryGetKnownResourceState(color_texture.GetNativeResource());
  ASSERT_TRUE(final_state.has_value());
  EXPECT_EQ(*final_state, ResourceStates::kShaderResource);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ExecuteAcceptsForwardPipeline)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get() },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "ForwardExecute", ViewId { 45U }, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());

  auto session = facade.Finalize();
  ASSERT_TRUE(session.has_value());

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(
    loop, [&]() -> oxygen::co::Co<void> { co_await session->Execute(); });

  EXPECT_FALSE(graphics_->draw_log_.draws.empty());
}

} // namespace
