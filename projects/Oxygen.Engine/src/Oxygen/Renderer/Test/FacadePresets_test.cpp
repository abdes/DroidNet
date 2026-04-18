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
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/FacadePresets.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

namespace {

using oxygen::EngineConfig;
using oxygen::Format;
using oxygen::Graphics;
using oxygen::IAsyncEngine;
using oxygen::observer_ptr;
using oxygen::PathFinder;
using oxygen::PathFinderConfig;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::console::Console;
using oxygen::engine::EngineModule;
using oxygen::engine::Renderer;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
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

class PresetTestEngine final : public IAsyncEngine {
public:
  PresetTestEngine(std::shared_ptr<Graphics> graphics,
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
  auto GetPlatformShared() const noexcept
    -> std::shared_ptr<oxygen::Platform> override
  {
    return {};
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
  auto SubscribeModuleAttached(oxygen::engine::ModuleAttachedCallback /*cb*/,
    bool /*replay_existing*/ = false) -> ModuleSubscription override
  {
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
};

class FacadePresetsHarnessTest : public ::testing::Test {
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
    framebuffer_ = MakeFramebuffer("FacadePresetsHarnessTest.Color");
  }

  [[nodiscard]] auto MakeFramebuffer(std::string_view debug_name) const
    -> std::shared_ptr<Framebuffer>
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

    auto color = graphics_->CreateTexture(color_desc);
    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeFrameSession() const -> Renderer::FrameSessionInput
  {
    return Renderer::FrameSessionInput {
      .frame_slot = oxygen::frame::Slot { 0U },
    };
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

  [[nodiscard]] auto AcquireRecorder(std::string_view name) const
  {
    return graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(QueueRole::kGraphics), name, false);
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
  std::unique_ptr<Renderer> renderer_ {};
};

class FacadePresetsOffscreenTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    asset_loader_ = std::make_unique<FakeAssetLoader>();
    engine_ = std::make_unique<PresetTestEngine>(graphics_,
      observer_ptr<oxygen::content::IAssetLoader> { asset_loader_.get() });

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    renderer_ = std::make_unique<Renderer>(
      std::weak_ptr<Graphics>(graphics_), std::move(config));
    engine_->AddModule(*renderer_);
    ASSERT_TRUE(
      renderer_->OnAttached(observer_ptr<IAsyncEngine> { engine_.get() }));

    framebuffer_ = MakeFramebuffer();
    scene_ = std::make_shared<oxygen::scene::Scene>("FacadePresetsScene", 16U);
    camera_node_ = scene_->CreateNode("Camera");
    ASSERT_TRUE(
      camera_node_.AttachCamera(std::make_unique<PerspectiveCamera>()));
  }

  [[nodiscard]] auto MakeFramebuffer() const -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = 96U;
    color_desc.height = 54U;
    color_desc.format = Format::kRGBA8UNorm;
    color_desc.texture_type = TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = "FacadePresetsOffscreenTest.Color";

    auto color = graphics_->CreateTexture(color_desc);
    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeFrameSession() const -> Renderer::FrameSessionInput
  {
    return Renderer::FrameSessionInput {
      .frame_slot = oxygen::frame::Slot { 0U },
    };
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::unique_ptr<FakeAssetLoader> asset_loader_ {};
  std::unique_ptr<PresetTestEngine> engine_ {};
  std::unique_ptr<Renderer> renderer_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
  std::shared_ptr<oxygen::scene::Scene> scene_ {};
  oxygen::scene::SceneNode camera_node_ {};
};

NOLINT_TEST_F(FacadePresetsHarnessTest,
  FullscreenGraphicsPassPresetProducesFinalizableFacade)
{
  auto facade = oxygen::renderer::harness::single_pass::presets::
    ForFullscreenGraphicsPass(*renderer_, MakeFrameSession(),
      observer_ptr<const Framebuffer> { framebuffer_.get() }, ViewId { 301U });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetRenderContext().current_view.view_id, ViewId { 301U });
}

NOLINT_TEST_F(FacadePresetsHarnessTest,
  PreparedSceneGraphicsPassPresetProducesFinalizableFacade)
{
  auto facade = oxygen::renderer::harness::single_pass::presets::
    ForPreparedSceneGraphicsPass(*renderer_, MakeFrameSession(),
      observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(),
      Renderer::PreparedFrameInput {
        .value = oxygen::engine::PreparedSceneFrame {} });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetRenderContext().current_view.view_id, ViewId { 91U });
}

NOLINT_TEST_F(
  FacadePresetsHarnessTest, SingleViewGraphPresetProducesExecutableHarness)
{
  auto executed = false;
  auto facade
    = oxygen::renderer::harness::render_graph::presets::ForSingleViewGraph(
      *renderer_, MakeFrameSession(),
      observer_ptr<const Framebuffer> { framebuffer_.get() },
      MakeResolvedViewInput(),
      [&executed](ViewId view_id, const oxygen::engine::RenderContext& context,
        oxygen::graphics::CommandRecorder&) -> oxygen::co::Co<void> {
        executed = true;
        EXPECT_EQ(view_id, ViewId { 91U });
        EXPECT_EQ(context.current_view.view_id, ViewId { 91U });
        co_return;
      });

  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  auto recorder = AcquireRecorder("FacadePresetsHarnessTest.Graph");
  ASSERT_NE(recorder, nullptr);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop,
    [&]() -> oxygen::co::Co<void> { co_await result->Execute(*recorder); });

  EXPECT_TRUE(executed);
}

NOLINT_TEST_F(
  FacadePresetsOffscreenTest, PreviewPresetProducesFinalizableOffscreenFacade)
{
  auto facade = oxygen::renderer::offscreen::scene::presets::ForPreview(
    *renderer_, MakeFrameSession(), observer_ptr { scene_.get() }, camera_node_,
    observer_ptr<const Framebuffer> { framebuffer_.get() });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->GetPipeline().get(), nullptr);
}

NOLINT_TEST_F(
  FacadePresetsOffscreenTest, CapturePresetProducesFinalizableOffscreenFacade)
{
  auto facade = oxygen::renderer::offscreen::scene::presets::ForCapture(
    *renderer_, MakeFrameSession(), observer_ptr { scene_.get() }, camera_node_,
    observer_ptr<const Framebuffer> { framebuffer_.get() });

  EXPECT_TRUE(facade.CanFinalize());
  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->GetPipeline().get(), nullptr);
}

} // namespace
