//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scripting/Test/Fakes/FakeScriptCompilationService.h>

namespace oxygen::scripting::test {

class FakeAsyncEngine final : public IAsyncEngine {
public:
  FakeAsyncEngine()
    : config_(MakeDefaultEngineConfig())
    , path_finder_config_(MakePathFinderConfig())
    , path_finder_(path_finder_config_, ResolveWorkingDirectory())
  {
  }

  ~FakeAsyncEngine() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeAsyncEngine)
  OXYGEN_MAKE_NON_MOVABLE(FakeAsyncEngine)

  auto GetAssetLoader() const noexcept
    -> observer_ptr<content::IAssetLoader> override
  {
    return {};
  }

  auto GetScriptCompilationService() noexcept
    -> scripting::IScriptCompilationService& override
  {
    return compilation_service_;
  }

  auto GetScriptCompilationService() const noexcept
    -> const scripting::IScriptCompilationService& override
  {
    return compilation_service_;
  }

  auto GetPathFinder() const noexcept -> const ::oxygen::PathFinder& override
  {
    return path_finder_;
  }

  auto GetGraphics() const noexcept
    -> std::weak_ptr<::oxygen::Graphics> override
  {
    return {};
  }

  auto GetPlatformShared() const noexcept
    -> std::shared_ptr<::oxygen::Platform> override
  {
    return {};
  }

  auto GetEngineConfig() const noexcept
    -> const ::oxygen::EngineConfig& override
  {
    return config_;
  }

  auto MutableConfig() noexcept -> ::oxygen::EngineConfig& { return config_; }

  auto GetConsole() noexcept -> ::oxygen::console::Console& override
  {
    return console_;
  }

  auto GetConsole() const noexcept -> const ::oxygen::console::Console& override
  {
    return console_;
  }

  auto IsRunning() const -> bool override { return running_; }

  auto Stop() -> void override { running_ = false; }

  auto SubscribeModuleAttached(::oxygen::engine::ModuleAttachedCallback /*cb*/,
    bool /*replay_existing*/ = false) -> ModuleSubscription override
  {
    return {};
  }

  auto GetModuleByType(oxygen::TypeId type_id) const noexcept
    -> std::optional<std::reference_wrapper<engine::EngineModule>> override
  {
    const auto it = modules_.find(type_id);
    if (it != modules_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  template <typename TModule>
  auto AddModule(std::unique_ptr<TModule> module) -> void
  {
    modules_.emplace(TModule::TypeInfo(),
      std::ref(static_cast<engine::EngineModule&>(*module)));
    owned_modules_.push_back(std::move(module));
  }

  auto AddModule(engine::EngineModule& module) -> void
  {
    modules_.emplace(module.GetTypeId(), std::ref(module));
  }

  auto ScriptCompilationService() noexcept -> FakeScriptCompilationService&
  {
    return compilation_service_;
  }

private:
  [[nodiscard]] static auto MakeDefaultEngineConfig() -> EngineConfig
  {
    EngineConfig config {};
    config.scripting.enable_hot_reload = false;
    return config;
  }

  [[nodiscard]] static auto ResolveWorkingDirectory() -> std::filesystem::path
  {
    std::error_code error;
    auto cwd = std::filesystem::current_path(error);
    if (error) {
      return ".";
    }
    return cwd;
  }

  [[nodiscard]] static auto MakePathFinderConfig()
    -> std::shared_ptr<const PathFinderConfig>
  {
    const auto cwd = ResolveWorkingDirectory();
    return PathFinderConfig::Create().WithWorkspaceRoot(cwd).BuildShared();
  }

  EngineConfig config_ {};
  std::shared_ptr<const PathFinderConfig> path_finder_config_;
  PathFinder path_finder_;
  console::Console console_ {};
  FakeScriptCompilationService compilation_service_ {};
  bool running_ { true };
  std::unordered_map<oxygen::TypeId,
    std::reference_wrapper<engine::EngineModule>>
    modules_ {};
  std::vector<std::unique_ptr<engine::EngineModule>> owned_modules_ {};
};

} // namespace oxygen::scripting::test
