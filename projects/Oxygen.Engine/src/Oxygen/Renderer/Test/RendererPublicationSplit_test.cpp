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
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

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
using oxygen::engine::LightCullingConfig;
using oxygen::engine::Renderer;
using oxygen::engine::VsmFrameBindings;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::renderer::testing::FakeAssetLoader;
using oxygen::renderer::testing::FakeGraphics;

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

class RendererPublicationTestEngine final : public IAsyncEngine {
public:
  RendererPublicationTestEngine(std::shared_ptr<Graphics> graphics,
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

class RendererPublicationSplitTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    asset_loader_ = std::make_unique<FakeAssetLoader>();
    engine_ = std::make_unique<RendererPublicationTestEngine>(graphics_,
      observer_ptr<oxygen::content::IAssetLoader> { asset_loader_.get() });

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    renderer_ = std::make_unique<Renderer>(std::weak_ptr<Graphics>(graphics_),
      std::move(config),
      oxygen::renderer::kPhase1DefaultRuntimeCapabilityFamilies);
    engine_->AddModule(*renderer_);
    ASSERT_TRUE(
      renderer_->OnAttached(observer_ptr<IAsyncEngine> { engine_.get() }));
    framebuffer_ = MakeFramebuffer();
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
    color_desc.debug_name = "RendererPublicationSplitTest.Color";

    auto depth_desc = TextureDesc {};
    depth_desc.width = 64U;
    depth_desc.height = 64U;
    depth_desc.format = oxygen::Format::kDepth32;
    depth_desc.texture_type = oxygen::TextureType::kTexture2D;
    depth_desc.use_clear_value = true;
    depth_desc.initial_state = ResourceStates::kDepthWrite;
    depth_desc.debug_name = "RendererPublicationSplitTest.Depth";

    auto color = graphics_->CreateTexture(color_desc);
    auto depth = graphics_->CreateTexture(depth_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    fb_desc.SetDepthAttachment({ .texture = depth });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeResolvedView() const -> Renderer::ResolvedViewInput
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
      .view_id = ViewId { 81U },
      .value = oxygen::ResolvedView(params),
    };
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::unique_ptr<FakeAssetLoader> asset_loader_ {};
  std::unique_ptr<RendererPublicationTestEngine> engine_ {};
  std::unique_ptr<Renderer> renderer_ {};
  std::shared_ptr<Framebuffer> framebuffer_ {};
};

NOLINT_TEST_F(RendererPublicationSplitTest,
  DynamicViewBindingRepublishRemainsStableAfterSplit)
{
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = oxygen::frame::Slot { 0U },
  });
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer = observer_ptr<const Framebuffer> { framebuffer_.get() },
  });
  facade.SetResolvedView(MakeResolvedView());

  auto result = facade.Finalize();
  ASSERT_TRUE(result.has_value());

  auto& render_context = result->GetRenderContext();
  render_context.pass_target
    = observer_ptr<const Framebuffer> { framebuffer_.get() };

  NOLINT_EXPECT_NO_THROW(renderer_->UpdateCurrentViewLightCullingConfig(
    render_context, LightCullingConfig {}));
  NOLINT_EXPECT_NO_THROW(renderer_->UpdateCurrentViewVirtualShadowFrameBindings(
    render_context, VsmFrameBindings {}));
  NOLINT_EXPECT_NO_THROW(renderer_->UpdateCurrentViewLightCullingConfig(
    render_context, LightCullingConfig {}));
}

} // namespace
