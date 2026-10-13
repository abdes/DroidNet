//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
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
class Platform;
}

namespace oxygen::scripting {
class IScriptCompilationService;
} // namespace oxygen::scripting

namespace oxygen::console {
class Console;
} // namespace oxygen::console

namespace oxygen::commands {
class CommandRegistry;
} // namespace oxygen::commands

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
    -> observer_ptr<content::IAssetLoader> = 0;

  [[nodiscard]] virtual auto GetScriptCompilationService() noexcept
    -> scripting::IScriptCompilationService& = 0;
  [[nodiscard]] virtual auto GetScriptCompilationService() const noexcept
    -> const scripting::IScriptCompilationService& = 0;

  [[nodiscard]] virtual auto GetPathFinder() const noexcept
    -> const ::oxygen::PathFinder& = 0;
  [[nodiscard]] virtual auto GetGraphics() const noexcept
    -> std::weak_ptr<::oxygen::Graphics> = 0;
  [[nodiscard]] virtual auto GetPlatformShared() const noexcept
    -> std::shared_ptr<::oxygen::Platform> = 0;

  [[nodiscard]] virtual auto GetEngineConfig() const noexcept
    -> const ::oxygen::EngineConfig& = 0;

  [[nodiscard]] virtual auto GetConsole() noexcept
    -> ::oxygen::console::Console& = 0;
  [[nodiscard]] virtual auto GetConsole() const noexcept
    -> const ::oxygen::console::Console& = 0;

  //! Every command, from the engine, its services and its modules.
  [[nodiscard]] virtual auto GetCommands() noexcept
    -> ::oxygen::commands::CommandRegistry& = 0;

  [[nodiscard]] virtual auto IsRunning() const -> bool = 0;
  virtual auto Stop() -> void = 0;

  //! Applied play-session state.
  enum class SessionState : std::uint8_t {
    kNone, //!< No session.
    kRunning, //!< A session runs and the engine is not paused.
    kPaused, //!< A session runs and the engine is paused.
  };

  // Pause, frame step and play sessions. Every call is a request, safe from
  // any thread; the engine applies requests once per frame, at frame start,
  // before any module's OnFrameStart. Queries report applied state.

  //! Stop simulation from the next frame on; rendering continues.
  virtual auto Pause() -> void = 0;
  //! Resume simulation; cancels frame steps still pending.
  virtual auto Resume() -> void = 0;
  [[nodiscard]] virtual auto IsPaused() const -> bool = 0;
  //! Pause if running, simulate `frames` frames, then stay paused.
  virtual auto Step(std::uint32_t frames) -> void = 0;
  //! Frame steps requested or applied and not yet started; 0 once every
  //! step has begun or `Resume` or a session change cancelled them.
  [[nodiscard]] virtual auto GetPendingSteps() const -> std::uint32_t = 0;

  //! Request a play session; false when one runs or is already requested.
  virtual auto BeginSession() -> bool = 0;
  //! End the running session at the next frame start; no-op without one.
  virtual auto EndSession() -> void = 0;
  [[nodiscard]] virtual auto GetSessionState() const -> SessionState = 0;

  using ModuleSubscription = ::oxygen::engine::ModuleManager::Subscription;
  virtual auto SubscribeModuleAttached(
    ::oxygen::engine::ModuleAttachedCallback cb, bool replay_existing)
    -> ModuleSubscription = 0;

  //! Receives each script source file the hot-reload watcher finds changed.
  using ScriptSourceChangedHandler
    = std::function<void(const std::filesystem::path&)>;
  //! Sets the one receiver of changed script sources; an empty handler stops
  //! delivery.
  virtual auto SetScriptSourceChangedHandler(ScriptSourceChangedHandler handler)
    -> void = 0;

  [[nodiscard]] virtual auto GetModuleByType(
    oxygen::TypeId type_id) const noexcept
    -> std::optional<std::reference_wrapper<engine::EngineModule>> = 0;

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
