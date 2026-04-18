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
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Pipeline/ForwardPipeline.h>
#include <Oxygen/Renderer/Pipeline/RendererCapability.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>

namespace {

using oxygen::EngineConfig;
using oxygen::Graphics;
using oxygen::IAsyncEngine;
using oxygen::observer_ptr;
using oxygen::PathFinder;
using oxygen::PathFinderConfig;
using oxygen::RendererConfig;
using oxygen::console::Console;
using oxygen::engine::EngineModule;
using oxygen::engine::Renderer;
using oxygen::graphics::QueueRole;
using oxygen::renderer::CapabilitySet;
using oxygen::renderer::ForwardPipeline;
using oxygen::renderer::PipelineCapabilityRequirements;
using oxygen::renderer::RendererCapabilityFamily;
using oxygen::renderer::ValidateCapabilityRequirements;
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

class RendererCapabilityTestEngine final : public IAsyncEngine {
public:
  explicit RendererCapabilityTestEngine(std::shared_ptr<Graphics> graphics)
    : graphics_(std::move(graphics))
    , path_finder_config_(PathFinderConfig::Create()
          .WithWorkspaceRoot(ResolveWorkingDirectory())
          .BuildShared())
    , path_finder_(path_finder_config_, ResolveWorkingDirectory())
  {
  }

  auto GetAssetLoader() const noexcept
    -> observer_ptr<oxygen::content::IAssetLoader> override
  {
    return {};
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
  std::shared_ptr<const PathFinderConfig> path_finder_config_;
  PathFinder path_finder_;
  Console console_ {};
  NullScriptCompilationService compilation_service_ {};
  bool running_ { true };
  std::unordered_map<oxygen::TypeId, std::reference_wrapper<EngineModule>>
    modules_ {};
};

class RendererCapabilityBindingTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    engine_ = std::make_unique<RendererCapabilityTestEngine>(graphics_);
  }

  [[nodiscard]] auto MakeRenderer(const CapabilitySet capabilities)
    -> std::shared_ptr<Renderer>
  {
    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    auto renderer = std::make_shared<Renderer>(
      std::weak_ptr<Graphics>(graphics_), std::move(config), capabilities);
    engine_->AddModule(*renderer);
    return renderer;
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::unique_ptr<RendererCapabilityTestEngine> engine_ {};
};

NOLINT_TEST(RendererCapabilityTest, ValidationSucceedsWhenRequiredFamiliesExist)
{
  const auto available = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | RendererCapabilityFamily::kFinalOutputComposition;
  const auto requirements = PipelineCapabilityRequirements {
    .required = RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kFinalOutputComposition,
    .optional = RendererCapabilityFamily::kDiagnosticsAndProfiling,
  };

  const auto validation
    = ValidateCapabilityRequirements(available, requirements);

  EXPECT_TRUE(validation.Ok());
  EXPECT_EQ(validation.available, available);
  EXPECT_EQ(validation.missing_required, RendererCapabilityFamily::kNone);
  EXPECT_EQ(validation.missing_optional,
    RendererCapabilityFamily::kDiagnosticsAndProfiling);
}

NOLINT_TEST(RendererCapabilityTest, ToStringListsFamiliesInStableOrder)
{
  const auto families = RendererCapabilityFamily::kShadowing
    | RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kFinalOutputComposition;

  EXPECT_EQ(oxygen::renderer::to_string(families),
    "ScenePreparation | Shadowing | FinalOutputComposition");
}

NOLINT_TEST_F(RendererCapabilityBindingTest,
  RendererValidationReportsMissingRequiredFamilies)
{
  const auto renderer
    = MakeRenderer(RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | RendererCapabilityFamily::kFinalOutputComposition);
  const auto requirements = PipelineCapabilityRequirements {
    .required = RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kGpuUploadAndAssetBinding,
    .optional = RendererCapabilityFamily::kShadowing
      | RendererCapabilityFamily::kEnvironmentLighting,
  };

  const auto validation
    = renderer->ValidateCapabilityRequirements(requirements);

  EXPECT_FALSE(validation.Ok());
  EXPECT_EQ(
    validation.missing_required, RendererCapabilityFamily::kScenePreparation);
  EXPECT_EQ(validation.missing_optional,
    RendererCapabilityFamily::kShadowing
      | RendererCapabilityFamily::kEnvironmentLighting);
}

NOLINT_TEST_F(RendererCapabilityBindingTest,
  ForwardPipelineRequiresExplicitRendererBindBeforeFrameExecution)
{
  const auto renderer = MakeRenderer(RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | RendererCapabilityFamily::kFinalOutputComposition);
  auto pipeline = ForwardPipeline(observer_ptr<IAsyncEngine> { engine_.get() });
  auto& pipeline_iface
    = static_cast<oxygen::renderer::RenderingPipeline&>(pipeline);

  NOLINT_EXPECT_DEATH(
    pipeline_iface.OnFrameStart(
      observer_ptr<oxygen::engine::FrameContext> {}, *renderer),
    "must be bound to the renderer");
}

NOLINT_TEST_F(
  RendererCapabilityBindingTest, ForwardPipelineBindsWithRequiredFamiliesOnly)
{
  const auto renderer = MakeRenderer(RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | RendererCapabilityFamily::kFinalOutputComposition);
  auto pipeline = ForwardPipeline(observer_ptr<IAsyncEngine> { engine_.get() });
  auto& pipeline_iface
    = static_cast<oxygen::renderer::RenderingPipeline&>(pipeline);

  NOLINT_EXPECT_NO_THROW(pipeline_iface.BindToRenderer(*renderer));
  NOLINT_EXPECT_NO_THROW(pipeline_iface.OnFrameStart(
    observer_ptr<oxygen::engine::FrameContext> {}, *renderer));
}

NOLINT_TEST_F(RendererCapabilityBindingTest,
  ForwardPipelineBindFailsWhenRequiredFamiliesAreMissing)
{
  const auto renderer
    = MakeRenderer(RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto pipeline = ForwardPipeline(observer_ptr<IAsyncEngine> { engine_.get() });
  auto& pipeline_iface
    = static_cast<oxygen::renderer::RenderingPipeline&>(pipeline);

  NOLINT_EXPECT_DEATH(
    pipeline_iface.BindToRenderer(*renderer), "missing required");
}

} // namespace
