//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>

#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

namespace {

using oxygen::EngineConfig;
using oxygen::Graphics;
using oxygen::IAsyncEngine;
using oxygen::observer_ptr;
using oxygen::PathFinder;
using oxygen::PathFinderConfig;
using oxygen::RendererConfig;
using oxygen::ViewId;
using oxygen::console::Console;
using oxygen::engine::EngineModule;
using oxygen::engine::Renderer;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::renderer::PipelineCapabilityRequirements;
using oxygen::renderer::PipelineFeature;
using oxygen::renderer::RendererCapabilityFamily;
using oxygen::renderer::testing::FakeAssetLoader;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::PerspectiveCamera;

class NullScriptCompilationService final
  : public oxygen::scripting::IScriptCompilationService {
public:
  auto RegisterCompiler(
    std::shared_ptr<const oxygen::scripting::IScriptCompiler> /*compiler*/)
    -> bool override
  {
    return true;
  }

  auto UnregisterCompiler(
    oxygen::data::pak::scripting::ScriptLanguage /*language*/) -> bool override
  {
    return true;
  }

  auto HasCompiler(
    oxygen::data::pak::scripting::ScriptLanguage /*language*/) const
    -> bool override
  {
    return false;
  }

  auto CompileAsync(Request /*request*/) -> oxygen::co::Co<Result> override
  {
    co_return Result {};
  }

  auto InFlightCount() const -> size_t override { return 0; }

  auto Subscribe(CompileKey compile_key, CompletionSubscriber /*subscriber*/)
    -> SubscriptionHandle override
  {
    return SubscriptionHandle {
      .compile_key = compile_key,
      .subscriber_id = SubscriberId {},
    };
  }

  auto Unsubscribe(const SubscriptionHandle& /*handle*/) -> bool override
  {
    return true;
  }

  auto AcquireForSlot(Request request, SlotAcquireCallbacks /*callbacks*/)
    -> SlotAcquireHandle override
  {
    return SlotAcquireHandle {
      .placeholder = nullptr,
      .subscription
      = SubscriptionHandle {
        .compile_key = request.compile_key,
        .subscriber_id = SubscriberId {},
      },
      .request = std::move(request),
    };
  }

  auto OnFrameStart(oxygen::engine::EngineTag /*tag*/) -> void override { }
  auto SetCacheBudget(size_t /*budget_bytes*/) -> void override { }
  auto SetDeferredPersistence(bool /*enabled*/) -> void override { }
  auto FlushPersistentCache() -> void override { }
};

class OffscreenSceneTestEngine final : public IAsyncEngine {
public:
  OffscreenSceneTestEngine(std::shared_ptr<Graphics> graphics,
    observer_ptr<oxygen::content::IAssetLoader> asset_loader)
    : graphics_(std::move(graphics))
    , asset_loader_(asset_loader)
    , path_finder_config_(PathFinderConfig::Create()
          .WithWorkspaceRoot(ResolveWorkingDirectory())
          .BuildShared())
    , path_finder_(path_finder_config_, ResolveWorkingDirectory())
  {
  }

  auto GetAssetLoader() const noexcept
    -> observer_ptr<oxygen::content::IAssetLoader> override
  {
    return asset_loader_;
  }

  auto GetScriptCompilationService() noexcept
    -> oxygen::scripting::IScriptCompilationService& override
  {
    return compilation_service_;
  }

  auto GetScriptCompilationService() const noexcept
    -> const oxygen::scripting::IScriptCompilationService& override
  {
    return compilation_service_;
  }

  auto GetPathFinder() const noexcept -> const PathFinder& override
  {
    return path_finder_;
  }

  auto GetGraphics() const noexcept -> std::weak_ptr<Graphics> override
  {
    return graphics_;
  }

  auto GetEngineConfig() const noexcept -> const EngineConfig& override
  {
    return config_;
  }

  auto GetConsole() noexcept -> Console& override { return console_; }

  auto GetConsole() const noexcept -> const Console& override
  {
    return console_;
  }

  auto IsRunning() const -> bool override { return running_; }

  auto Stop() -> void override { running_ = false; }

  auto SubscribeModuleAttached(oxygen::engine::ModuleAttachedCallback cb,
    bool replay_existing = false) -> ModuleSubscription override
  {
    if (replay_existing) {
      for (const auto& [_, module] : modules_) {
        cb(oxygen::engine::ModuleEvent {
          .type_id = module.get().GetTypeId(),
          .name = std::string(module.get().GetName()),
          .module = observer_ptr<EngineModule> { &module.get() },
        });
      }
    }
    attached_callbacks_.push_back(std::move(cb));
    return {};
  }

  auto GetModuleByType(oxygen::TypeId type_id) const noexcept
    -> std::optional<std::reference_wrapper<EngineModule>> override
  {
    const auto it = modules_.find(type_id);
    if (it == modules_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  auto AddModule(EngineModule& module) -> void
  {
    modules_.emplace(module.GetTypeId(), std::ref(module));
    for (const auto& callback : attached_callbacks_) {
      callback(oxygen::engine::ModuleEvent {
        .type_id = module.GetTypeId(),
        .name = std::string(module.GetName()),
        .module = observer_ptr<EngineModule> { &module },
      });
    }
  }

private:
  [[nodiscard]] static auto ResolveWorkingDirectory() -> std::filesystem::path
  {
    std::error_code error;
    auto cwd = std::filesystem::current_path(error);
    if (error) {
      return ".";
    }
    return cwd;
  }

  EngineConfig config_ {};
  std::shared_ptr<Graphics> graphics_ {};
  observer_ptr<oxygen::content::IAssetLoader> asset_loader_ { nullptr };
  std::shared_ptr<const PathFinderConfig> path_finder_config_;
  PathFinder path_finder_;
  Console console_ {};
  NullScriptCompilationService compilation_service_ {};
  bool running_ { true };
  std::unordered_map<oxygen::TypeId, std::reference_wrapper<EngineModule>>
    modules_ {};
  std::vector<oxygen::engine::ModuleAttachedCallback> attached_callbacks_ {};
};

class StubOffscreenPipeline final : public oxygen::renderer::RenderingPipeline {
  OXYGEN_TYPED(StubOffscreenPipeline)
public:
  [[nodiscard]] auto GetSupportedFeatures() const -> PipelineFeature override
  {
    return PipelineFeature::kNone;
  }

  [[nodiscard]] auto GetCapabilityRequirements() const
    -> PipelineCapabilityRequirements override
  {
    return {};
  }

  auto OnFrameStart(observer_ptr<oxygen::engine::FrameContext>, Renderer&)
    -> void override
  {
    ++frame_start_calls;
  }

  auto OnPublishViews(observer_ptr<oxygen::engine::FrameContext> frame_ctx,
    Renderer& renderer, oxygen::scene::Scene& /*scene*/,
    std::span<const oxygen::renderer::CompositionView> view_descs,
    Framebuffer* composite_target) -> oxygen::co::Co<> override
  {
    ++publish_views_calls;
    for (const auto& desc : view_descs) {
      oxygen::engine::ViewContext view_ctx {};
      view_ctx.view = desc.view;
      view_ctx.metadata = {
        .name = std::string(desc.name),
        .purpose = "scene",
        .is_scene_view = true,
        .with_atmosphere = desc.with_atmosphere,
        .exposure_view_id = desc.exposure_source_view_id,
      };
      view_ctx.render_target = observer_ptr { composite_target };
      view_ctx.composite_source = observer_ptr { composite_target };

      const auto published_id = frame_ctx->RegisterView(std::move(view_ctx));
      published_view_id_ = published_id;

      auto camera = *desc.camera;
      oxygen::renderer::SceneCameraViewResolver resolver(
        [camera](const ViewId&) -> oxygen::scene::SceneNode { return camera; },
        desc.view.viewport);
      renderer.RegisterViewRenderGraph(
        published_id,
        [this](ViewId view_id, const oxygen::engine::RenderContext& context,
          oxygen::graphics::CommandRecorder&) -> oxygen::co::Co<void> {
          ++graph_execute_calls;
          last_executed_view_id = view_id;
          EXPECT_EQ(context.current_view.view_id, view_id);
          EXPECT_NE(context.pass_target.get(), nullptr);
          co_return;
        },
        resolver(published_id));
    }
    co_return;
  }

  auto OnPreRender(observer_ptr<oxygen::engine::FrameContext>, Renderer&,
    std::span<const oxygen::renderer::CompositionView>)
    -> oxygen::co::Co<> override
  {
    ++pre_render_calls;
    co_return;
  }

  auto OnCompositing(
    observer_ptr<oxygen::engine::FrameContext>, std::shared_ptr<Framebuffer>)
    -> oxygen::co::Co<oxygen::engine::CompositionSubmission> override
  {
    ++compositing_calls;
    co_return {};
  }

  auto ClearBackbufferReferences() -> void override { }

  mutable int frame_start_calls { 0 };
  mutable int publish_views_calls { 0 };
  mutable int pre_render_calls { 0 };
  mutable int compositing_calls { 0 };
  mutable int graph_execute_calls { 0 };
  mutable ViewId published_view_id_ {};
  mutable ViewId last_executed_view_id {};
};

class OffscreenSceneFacadeTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    asset_loader_ = std::make_unique<FakeAssetLoader>();
    engine_ = std::make_unique<OffscreenSceneTestEngine>(graphics_,
      observer_ptr<oxygen::content::IAssetLoader> { asset_loader_.get() });
    renderer_ = MakeAttachedRenderer(RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kShadowing
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition
      | RendererCapabilityFamily::kDiagnosticsAndProfiling);

    framebuffer_ = MakeFramebuffer();
    scene_
      = std::make_shared<oxygen::scene::Scene>("OffscreenSceneFacadeTest", 32U);
    camera_node_ = scene_->CreateNode("Camera");
    ASSERT_TRUE(
      camera_node_.AttachCamera(std::make_unique<PerspectiveCamera>()));
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
    color_desc.debug_name = "OffscreenSceneFacadeTest.Color";

    auto color = graphics_->CreateTexture(color_desc);
    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeAttachedRenderer(
    const oxygen::renderer::CapabilitySet capabilities)
    -> std::unique_ptr<Renderer>
  {
    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    auto renderer = std::make_unique<Renderer>(
      std::weak_ptr<Graphics>(graphics_), std::move(config), capabilities);
    engine_->AddModule(*renderer);
    EXPECT_TRUE(
      renderer->OnAttached(observer_ptr<IAsyncEngine> { engine_.get() }));
    return renderer;
  }

  [[nodiscard]] auto MakeViewIntent() const -> Renderer::OffscreenSceneViewInput
  {
    auto view = oxygen::View {};
    view.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    return Renderer::OffscreenSceneViewInput::FromCamera(
      "Preview", oxygen::kInvalidViewId, view, camera_node_);
  }

  [[nodiscard]] auto MakeOutputTarget() const -> Renderer::OutputTargetInput
  {
    return Renderer::OutputTargetInput {
      .framebuffer = observer_ptr<const Framebuffer>(framebuffer_.get()),
    };
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::unique_ptr<FakeAssetLoader> asset_loader_ {};
  std::unique_ptr<OffscreenSceneTestEngine> engine_ {};
  std::unique_ptr<Renderer> renderer_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
  std::shared_ptr<oxygen::scene::Scene> scene_ {};
  oxygen::scene::SceneNode camera_node_ {};
};

NOLINT_TEST_F(OffscreenSceneFacadeTest, CanFinalizeTracksRequiredInputs)
{
  auto facade = renderer_->ForOffscreenScene();

  EXPECT_FALSE(facade.CanFinalize());
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  EXPECT_FALSE(facade.CanFinalize());
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  EXPECT_FALSE(facade.CanFinalize());
  facade.SetViewIntent(MakeViewIntent());
  EXPECT_FALSE(facade.CanFinalize());
  facade.SetOutputTarget(MakeOutputTarget());
  EXPECT_TRUE(facade.CanFinalize());
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ValidateRejectsMissingCamera)
{
  auto facade = renderer_->ForOffscreenScene();
  auto view_intent = MakeViewIntent();
  view_intent = Renderer::OffscreenSceneViewInput::FromCamera("Preview",
    oxygen::kInvalidViewId, oxygen::View {}, oxygen::scene::SceneNode {});

  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  facade.SetViewIntent(std::move(view_intent));
  facade.SetOutputTarget(MakeOutputTarget());

  const auto report = facade.Validate();

  EXPECT_FALSE(report.Ok());
  ASSERT_FALSE(report.issues.empty());
  EXPECT_EQ(report.issues.back().code, "view_intent.invalid_camera");
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, ValidateRejectsInvalidFrameSession)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 99U },
    .delta_time_seconds = 0.0F,
  });
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  facade.SetViewIntent(MakeViewIntent());
  facade.SetOutputTarget(MakeOutputTarget());

  const auto report = facade.Validate();

  EXPECT_FALSE(report.Ok());
  EXPECT_EQ(report.issues.front().code, "frame_session.out_of_bounds_slot");
}

NOLINT_TEST_F(OffscreenSceneFacadeTest, FinalizeCreatesDefaultForwardPipeline)
{
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  facade.SetViewIntent(MakeViewIntent());
  facade.SetOutputTarget(MakeOutputTarget());

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->GetPipeline().get(), nullptr);
  EXPECT_EQ(result->GetViewId(), ViewId { 1U });
}

NOLINT_TEST_F(OffscreenSceneFacadeTest,
  FinalizeDefaultPipelineDoesNotRequireFinalOutputCompositionCapability)
{
  auto renderer
    = MakeAttachedRenderer(RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kShadowing
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kDiagnosticsAndProfiling);

  auto facade = renderer->ForOffscreenScene();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  facade.SetViewIntent(MakeViewIntent());
  facade.SetOutputTarget(MakeOutputTarget());

  auto result = facade.Finalize();

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->GetPipeline().get(), nullptr);
}

NOLINT_TEST_F(
  OffscreenSceneFacadeTest, ExecuteRunsSceneThroughExplicitPipelineHooks)
{
  StubOffscreenPipeline pipeline;
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
    .frame_sequence = oxygen::frame::SequenceNumber { 3U },
  });
  facade.SetSceneSource(Renderer::SceneSourceInput {
    .scene = observer_ptr<oxygen::scene::Scene> { scene_.get() },
  });
  facade.SetViewIntent(MakeViewIntent());
  facade.SetOutputTarget(MakeOutputTarget());
  facade.SetPipeline(Renderer::OffscreenPipelineInput::Borrowed(
    observer_ptr<oxygen::renderer::RenderingPipeline> { &pipeline }));

  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(
    loop, [&]() -> oxygen::co::Co<void> { co_await result->Execute(); });

  EXPECT_EQ(pipeline.frame_start_calls, 1);
  EXPECT_EQ(pipeline.publish_views_calls, 1);
  EXPECT_EQ(pipeline.pre_render_calls, 1);
  EXPECT_EQ(pipeline.compositing_calls, 1);
  EXPECT_EQ(pipeline.graph_execute_calls, 1);
  EXPECT_EQ(pipeline.last_executed_view_id, pipeline.published_view_id_);
}

} // namespace
