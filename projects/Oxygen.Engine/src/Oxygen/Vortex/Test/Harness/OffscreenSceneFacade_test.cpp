//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/FacadePresets.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
}

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
        | RendererCapabilityFamily::kFinalOutputComposition,
    };
    renderer_ = {
      new Renderer(
        std::weak_ptr<Graphics>(graphics_), std::move(config), capabilities),
      DestroyRenderer,
    };
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
    fb_desc.AddColorAttachment({
      .texture = color,
    });
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
      .frame_slot = oxygen::frame::Slot { 1U, },
      .frame_sequence = oxygen::frame::SequenceNumber { 17U, },
      .delta_time_seconds = 1.0F / 60.0F,
      .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
    };
  }

  [[nodiscard]] auto MakeOutputTarget() const -> Renderer::OutputTargetInput
  {
    return Renderer::OutputTargetInput {
      .framebuffer = oxygen::observer_ptr<Framebuffer> { framebuffer_.get(), },
    };
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Framebuffer> framebuffer_;
  std::shared_ptr<Scene> scene_;
  oxygen::scene::SceneNode camera_ {};
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(OffscreenSceneFacadeTest, ViewInputCopiesAndMovesOwnTheirNames)
{
  using Input = Renderer::OffscreenSceneViewInput;
  for (const auto& name : {
         std::string("Short"),
         std::string(160, 'N'),
       }) {
    SCOPED_TRACE(name);
    auto source = Input::FromCamera(name,
      ViewId {
        42U,
      },
      MakeView(), camera_);
    auto copy = Input(source);
    auto assigned = Input {};
    assigned = source;
    EXPECT_NE(copy.ViewIntent().name.data(), source.ViewIntent().name.data());
    EXPECT_NE(
      assigned.ViewIntent().name.data(), source.ViewIntent().name.data());
    auto moved = Input(std::move(copy));
    auto move_assigned = Input {};
    move_assigned = std::move(assigned);
    // The test deliberately inspects the supported moved-from view to verify it
    // no longer aliases the destination name.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_NE(moved.ViewIntent().name.data(), copy.ViewIntent().name.data());
    EXPECT_NE(move_assigned.ViewIntent().name.data(),
      // The test deliberately inspects the supported moved-from view to verify
      // it no longer aliases the destination name.
      // NOLINTNEXTLINE(bugprone-use-after-move)
      assigned.ViewIntent().name.data());
    source = Input {};
    copy = Input {};
    assigned = Input {};
    EXPECT_EQ(moved.ViewIntent().name, name);
    EXPECT_EQ(move_assigned.ViewIntent().name, name);
    EXPECT_EQ(moved.ViewIntent().id,
      (ViewId {
        42U,
      }));
    EXPECT_EQ(move_assigned.ViewIntent().id,
      (ViewId {
        42U,
      }));
  }
}

NOLINT_TEST_F(OffscreenSceneFacadeTest,
  SessionRetainsNameAndExposureAfterBuilderDestruction)
{
  struct Capture final : oxygen::vortex::IViewExtension {
    std::vector<std::string> names;
    std::vector<std::optional<oxygen::scene::ExposureSettings>> exposures;
    auto OnViewSetup(const oxygen::vortex::ViewSetupContext& context)
      -> void override
    {
      const auto* entry = context.render_context.GetActiveViewEntry();
      ASSERT_NE(entry, nullptr);
      names.push_back(entry->debug_name);
      exposures.push_back(
        context.render_context.current_view.exposure_override);
    }
  };
  auto capture = std::make_shared<Capture>();
  renderer_->RegisterViewExtension(capture);
  const auto name = std::string(160, 'N');
  for (const bool override_enabled : {
         true,
         false,
       }) {
    auto result
      = [&] -> oxygen::Result<Renderer::ValidatedOffscreenSceneSession,
              Renderer::ValidationReport> {
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession(MakeFrameSession());
      facade.SetSceneSource({ .scene = oxygen::observer_ptr {
                                scene_.get(),
                              } });
      facade.SetOutputTarget(MakeOutputTarget());
      auto exposure = oxygen::scene::ExposureSettings {};
      exposure.manual_ev = 4;
      exposure.speed_down = .5F;
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(name,
        ViewId {
          42U,
        },
        MakeView(), camera_)
                     .SetExposureOverride(exposure);
      if (!override_enabled) {
        input.SetExposureOverride(std::nullopt);
      }
      facade.SetViewIntent(input);
      return facade.Finalize();
    }();
    if (!result.has_value()) {
      FAIL() << "Expected result to have a value";
    }
    auto session = std::move(result).value();
    ASSERT_TRUE(session.ExecuteNow());
    auto frame = oxygen::engine::FrameContext {};
    frame.SetScene(oxygen::observer_ptr {
      scene_.get(),
    });
    frame.SetFrameSequenceNumber(
      oxygen::frame::SequenceNumber {
        18U,
      },
      oxygen::engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      oxygen::frame::Slot {
        2U,
      },
      oxygen::engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(oxygen::observer_ptr {
      &frame,
    });
    ASSERT_TRUE(session.ExecuteInsideFrame(frame));
    renderer_->OnFrameEnd(oxygen::observer_ptr {
      &frame,
    });
  }
  ASSERT_EQ(capture->names.size(), 4U);
  ASSERT_EQ(capture->exposures.size(), 4U);
  for (unsigned i = 0; i < 4; ++i) {
    EXPECT_EQ(capture->names.at(i), name);
    const auto& exposure = capture->exposures.at(i);
    if (i < 2) {
      if (!exposure.has_value()) {
        FAIL()
          << "Expected the first two views to keep their exposure override";
      }
      EXPECT_EQ(exposure->manual_ev, 4);
      EXPECT_EQ(exposure->speed_down, .5F);
    } else {
      ASSERT_FALSE(exposure.has_value());
    }
  }
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, BothExecutionPathsPreserveLocalScissor)
{
  struct Capture final : oxygen::vortex::IViewExtension {
    std::vector<oxygen::Scissors> scissors;
    auto OnViewSetup(const oxygen::vortex::ViewSetupContext& context)
      -> void override
    {
      scissors.push_back(
        context.render_context.current_view.resolved_view->Scissor());
    }
  };
  auto capture = std::make_shared<Capture>();
  renderer_->RegisterViewExtension(capture);
  auto view = MakeView();
  view.scissor = {
    .left = 8,
    .top = 12,
    .right = 48,
    .bottom = 52,
  };
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource({ .scene = oxygen::observer_ptr {
                            scene_.get(),
                          } });
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera("Inset",
    ViewId {
      42U,
    },
    view, camera_));
  auto session = facade.Finalize();
  if (!session.has_value()) {
    FAIL() << "Expected session to have a value";
  }
  ASSERT_TRUE(session->ExecuteNow());
  auto frame = oxygen::engine::FrameContext {};
  frame.SetScene(oxygen::observer_ptr {
    scene_.get(),
  });
  frame.SetFrameSequenceNumber(
    oxygen::frame::SequenceNumber {
      18U,
    },
    oxygen::engine::internal::EngineTagFactory::Get());
  frame.SetFrameSlot(
    oxygen::frame::Slot {
      2U,
    },
    oxygen::engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(oxygen::observer_ptr {
    &frame,
  });
  ASSERT_TRUE(session->ExecuteInsideFrame(frame));
  renderer_->OnFrameEnd(oxygen::observer_ptr {
    &frame,
  });
  ASSERT_EQ(capture->scissors.size(), 2U);
  for (const auto& scissor : capture->scissors) {
    EXPECT_EQ(scissor.left, 8);
    EXPECT_EQ(scissor.top, 12);
    EXPECT_EQ(scissor.right, 48);
    EXPECT_EQ(scissor.bottom, 52);
  }
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ValidateRejectsInvalidViewId)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
    "InvalidOffscreen", oxygen::kInvalidViewId, MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) -> auto {
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
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("NoColorTarget",
      ViewId {
        46U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer
    = oxygen::observer_ptr<Framebuffer> { framebuffer_without_color.get(), },
  });

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) -> auto {
    return issue.code == "output_target.invalid_framebuffer";
  }));
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PresetsFinalizeWithRenderableViewIds)
{
  auto preview = oxygen::vortex::offscreen::scene::presets::ForPreview(
    *renderer_, MakeFrameSession(),
    oxygen::observer_ptr<Scene> {
      scene_.get(),
    },
    camera_,
    oxygen::observer_ptr<Framebuffer> {
      framebuffer_.get(),
    });
  auto capture = oxygen::vortex::offscreen::scene::presets::ForCapture(
    *renderer_, MakeFrameSession(),
    oxygen::observer_ptr<Scene> {
      scene_.get(),
    },
    camera_,
    oxygen::observer_ptr<Framebuffer> {
      framebuffer_.get(),
    });

  auto preview_session = preview.Finalize();
  auto capture_session = capture.Finalize();

  if (!preview_session.has_value()) {

    FAIL() << "Expected preview_session to have a value";
  }
  if (!capture_session.has_value()) {
    FAIL() << "Expected capture_session to have a value";
  }
  EXPECT_NE(preview_session->GetViewId(), oxygen::kInvalidViewId);
  EXPECT_NE(capture_session->GetViewId(), oxygen::kInvalidViewId);
  EXPECT_NE(preview_session->GetViewId(), capture_session->GetViewId());
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineDefaultsToDeferred)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("DefaultPipeline",
      ViewId {
        43U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  auto session = facade.Finalize();

  if (!session.has_value()) {

    FAIL() << "Expected session to have a value";
  }
  EXPECT_EQ(session->GetPipelineShadingMode(), ShadingMode::kDeferred);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineCanSelectForward)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("ForwardPipeline",
      ViewId {
        44U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());

  auto session = facade.Finalize();

  if (!session.has_value()) {

    FAIL() << "Expected session to have a value";
  }
  EXPECT_EQ(session->GetPipelineShadingMode(), ShadingMode::kForward);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PipelineCarriesFeatureProfile)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("NoEnvironmentPipeline",
      ViewId {
        47U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput {
    .feature_profile = CompositionView::ViewFeatureProfile::kNoEnvironment,
  });

  auto session = facade.Finalize();

  if (!session.has_value()) {

    FAIL() << "Expected session to have a value";
  }
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
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("ShadowOnlyPipeline",
      ViewId {
        48U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::ShadowOnly());

  const auto report = facade.Validate();

  ASSERT_FALSE(report.Ok());
  EXPECT_TRUE(std::ranges::any_of(report.issues, [](const auto& issue) -> auto {
    return issue.code == "pipeline.missing_required_capabilities";
  }));
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ExecuteRendersIntoOutputTarget)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("OffscreenExecute",
      ViewId {
        42U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());

  auto session = facade.Finalize();
  if (!session.has_value()) {
    FAIL() << "Expected session to have a value";
  }

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(
    // Synchronous execution finishes while the session-owned closure and its
    // captured test locals are alive.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    loop, [&] -> oxygen::co::Co<void> { co_await session->Execute(); });

  EXPECT_FALSE(graphics_->draw_log_.draws.empty());
  const auto& color_texture
    = *framebuffer_->GetDescriptor().color_attachments.front().texture;
  auto queue = graphics_->GetCommandQueue(QueueRole::kGraphics);
  ASSERT_NE(queue.get(), nullptr);
  const auto final_state
    = queue->TryGetKnownResourceState(color_texture.GetNativeResource());
  if (!final_state.has_value()) {
    FAIL() << "Expected final_state to have a value";
  }
  EXPECT_EQ(*final_state, ResourceStates::kShaderResource);
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ExecuteAcceptsForwardPipeline)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), },
  });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("ForwardExecute",
      ViewId {
        45U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());

  auto session = facade.Finalize();
  if (!session.has_value()) {
    FAIL() << "Expected session to have a value";
  }

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(
    // Synchronous execution finishes while the session-owned closure and its
    // captured test locals are alive.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    loop, [&] -> oxygen::co::Co<void> { co_await session->Execute(); });

  EXPECT_FALSE(graphics_->draw_log_.draws.empty());
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, PausedFrameSessionCanFinalize)
{
  auto frame = MakeFrameSession();
  frame.delta_time_seconds = 0.0F;
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(frame);
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = oxygen::observer_ptr<Scene> { scene_.get(), }, });
  facade.SetViewIntent(
    Renderer::OffscreenSceneViewInput::FromCamera("PausedOffscreen",
      ViewId {
        51U,
      },
      MakeView(), camera_));
  facade.SetOutputTarget(MakeOutputTarget());
  EXPECT_TRUE(facade.Validate().Ok());
  EXPECT_TRUE(facade.Finalize().has_value());
}

NOLINT_TEST_F(
  OffscreenSceneFacadeTest, SharingRejectsMissingAndStatelessSources)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(MakeFrameSession());
  facade.SetSceneSource({ .scene = oxygen::observer_ptr {
                            scene_.get(),
                          } });
  facade.SetOutputTarget(MakeOutputTarget());
  auto input = Renderer::OffscreenSceneViewInput::FromCamera("SharedOffscreen",
    ViewId {
      42U,
    },
    MakeView(), camera_);
  input.SetExposureSourceViewId(ViewId {
    800U,
  });
  input.SetViewStateHandle(CompositionView::ViewStateHandle {
    92U,
  });
  facade.SetViewIntent(input);
  EXPECT_FALSE(facade.Finalize().has_value());
  auto frame = oxygen::engine::FrameContext {};
  auto source = CompositionView::ForScene(
    ViewId {
      800U,
    },
    MakeView(), camera_);
  ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = source,
                .render_target = oxygen::observer_ptr { framebuffer_.get(), }, }),
    oxygen::kInvalidViewId);
  EXPECT_FALSE(facade.Finalize().has_value());
  source.view_state_handle = CompositionView::ViewStateHandle {
    90U,
  };
  ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = source,
                .render_target = oxygen::observer_ptr { framebuffer_.get(), }, }),
    oxygen::kInvalidViewId);
  input.SetViewStateHandle(CompositionView::kInvalidViewStateHandle);
  facade.SetViewIntent(input);
  // A source becoming persistent does not make a stateless borrower valid.
  EXPECT_FALSE(facade.Finalize().has_value());
  const auto queued
    = renderer_->QueueExposureTransition(source.view_state_handle,
      oxygen::vortex::ExposureTransitionPolicy::kPreserve);
  if (!queued.has_value()) {
    FAIL() << "Expected queued to have a value";
  }
  input.SetViewStateHandle(source.view_state_handle);
  facade.SetViewIntent(input);
  EXPECT_FALSE(facade.Finalize().has_value());
  auto other = source;
  other.id = ViewId {
    802U,
  };
  other.view_state_handle = CompositionView::ViewStateHandle {
    91U,
  };
  ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = other,
                .render_target = oxygen::observer_ptr { framebuffer_.get(), }, }),
    oxygen::kInvalidViewId);
  input.SetViewStateHandle(other.view_state_handle);
  facade.SetViewIntent(input);
  EXPECT_FALSE(facade.Finalize().has_value());
  const auto transition
    = renderer_->InspectExposureTransition(source.view_state_handle);
  if (!transition.has_value()) {
    FAIL() << "Expected transition to have a value";
  }
  EXPECT_EQ(transition->request, *queued);
  EXPECT_EQ(
    transition->phase, oxygen::vortex::ExposureTransitionPhase::kQueued);
  input.SetViewStateHandle(CompositionView::ViewStateHandle {
    92U,
  });
  facade.SetViewIntent(input);
  auto session = facade.Finalize();
  if (!session.has_value()) {
    FAIL() << "Expected session to have a value";
  }
  renderer_->RemovePublishedRuntimeView(frame, source.id);
  EXPECT_FALSE(facade.Finalize().has_value());
  graphics_->draw_log_.draws.clear();
  session->ExecuteNow();
  EXPECT_TRUE(graphics_->draw_log_.draws.empty());
  session->ExecuteInsideFrame(frame);
  EXPECT_TRUE(graphics_->draw_log_.draws.empty());
}

} // namespace
