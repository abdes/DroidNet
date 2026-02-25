//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Engine/ModuleManager.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen {
class Graphics;
}

namespace oxygen::scripting {
class IScriptCompilationService;
} // namespace oxygen::scripting

namespace oxygen::console {
class Console;
} // namespace oxygen::console

namespace oxygen::engine {
class EngineModule;
} // namespace oxygen::engine

namespace oxygen {

class IAsyncEngine {
public:
  IAsyncEngine() = default;
  virtual ~IAsyncEngine() = default;

  OXYGEN_MAKE_NON_COPYABLE(IAsyncEngine)
  OXYGEN_MAKE_NON_MOVABLE(IAsyncEngine)

  [[nodiscard]] virtual auto GetAssetLoader() const noexcept
    -> observer_ptr<content::IAssetLoader>
    = 0;

  [[nodiscard]] virtual auto GetScriptCompilationService() noexcept
    -> scripting::IScriptCompilationService& = 0;
  [[nodiscard]] virtual auto GetScriptCompilationService() const noexcept
    -> const scripting::IScriptCompilationService& = 0;

  [[nodiscard]] virtual auto GetPathFinder() const noexcept
    -> const ::oxygen::PathFinder& = 0;
  [[nodiscard]] virtual auto GetGraphics() const noexcept
    -> std::weak_ptr<::oxygen::Graphics>
    = 0;

  [[nodiscard]] virtual auto GetEngineConfig() const noexcept
    -> const ::oxygen::EngineConfig& = 0;

  [[nodiscard]] virtual auto GetConsole() noexcept
    -> ::oxygen::console::Console& = 0;
  [[nodiscard]] virtual auto GetConsole() const noexcept
    -> const ::oxygen::console::Console& = 0;

  [[nodiscard]] virtual auto IsRunning() const -> bool = 0;
  virtual auto Stop() -> void = 0;

  using ModuleSubscription = ::oxygen::engine::ModuleManager::Subscription;
  virtual auto SubscribeModuleAttached(
    ::oxygen::engine::ModuleAttachedCallback cb, bool replay_existing = false)
    -> ModuleSubscription
    = 0;

  [[nodiscard]] virtual auto GetModuleByType(
    oxygen::TypeId type_id) const noexcept
    -> std::optional<std::reference_wrapper<engine::EngineModule>>
    = 0;

  template <oxygen::IsTyped ModuleT>
  [[nodiscard]] auto GetModule() const noexcept
    -> std::optional<std::reference_wrapper<ModuleT>>
  {
    const auto module = GetModuleByType(ModuleT::ClassTypeId());
    if (!module.has_value()) {
      return std::nullopt;
    }
    return std::ref(static_cast<ModuleT&>(module->get()));
  }
};

} // namespace oxygen
