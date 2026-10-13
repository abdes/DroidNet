//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/IAsyncEngine.h>

namespace oxygen::control::testing {

//! An engine with a real command registry and console, and a run state
//! (pause, steps, session, stop) the tests drive.
/*!
 A plain fake: the dispatcher and the server use only the registry, the
 console and the run state. Services they never touch (path finder, script
 compilation) abort when called, so a new dependency fails loudly.
*/
class TestEngine final : public IAsyncEngine {
public:
  TestEngine() = default;
  ~TestEngine() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TestEngine)
  OXYGEN_MAKE_NON_MOVABLE(TestEngine)

  //! Plays the engine's frame start: true when this frame simulates.
  auto BeginFrame() -> bool
  {
    if (!paused) {
      return true;
    }
    if (pending_steps > 0U) {
      --pending_steps;
      return true;
    }
    return false;
  }

  auto GetAssetLoader() const noexcept
    -> observer_ptr<content::IAssetLoader> override
  {
    return {};
  }

  auto GetScriptCompilationService() noexcept
    -> scripting::IScriptCompilationService& override
  {
    ABORT_F("TestEngine has no script compilation service");
  }

  auto GetScriptCompilationService() const noexcept
    -> const scripting::IScriptCompilationService& override
  {
    ABORT_F("TestEngine has no script compilation service");
  }

  auto GetPathFinder() const noexcept -> const PathFinder& override
  {
    ABORT_F("TestEngine has no path finder");
  }

  auto GetGraphics() const noexcept -> std::weak_ptr<Graphics> override
  {
    return {};
  }

  auto GetPlatformShared() const noexcept -> std::shared_ptr<Platform> override
  {
    return {};
  }

  auto GetEngineConfig() const noexcept -> const EngineConfig& override
  {
    return config_;
  }

  auto GetConsole() noexcept -> console::Console& override { return console_; }

  auto GetConsole() const noexcept -> const console::Console& override
  {
    return console_;
  }

  auto GetCommands() noexcept -> commands::CommandRegistry& override
  {
    return commands_;
  }

  auto IsRunning() const -> bool override { return !stopped; }

  auto Stop() -> void override { stopped = true; }

  auto Pause() -> void override { paused = true; }

  auto Resume() -> void override
  {
    paused = false;
    pending_steps = 0U;
  }

  auto IsPaused() const -> bool override { return paused; }

  auto Step(const std::uint32_t frames) -> void override
  {
    paused = true;
    pending_steps += frames;
  }

  auto GetPendingSteps() const -> std::uint32_t override
  {
    return pending_steps;
  }

  auto BeginSession() -> bool override
  {
    if (session != SessionState::kNone) {
      return false;
    }
    session = SessionState::kRunning;
    return true;
  }

  auto EndSession() -> void override { session = SessionState::kNone; }

  auto GetSessionState() const -> SessionState override { return session; }

  auto SubscribeModuleAttached(engine::ModuleAttachedCallback /*cb*/,
    bool /*replay_existing*/) -> ModuleSubscription override
  {
    return {};
  }

  auto SetScriptSourceChangedHandler(ScriptSourceChangedHandler /*handler*/)
    -> void override
  {
  }

  auto GetModuleByType(TypeId /*type_id*/) const noexcept
    -> std::optional<std::reference_wrapper<engine::EngineModule>> override
  {
    return std::nullopt;
  }

  bool paused { false };
  std::uint32_t pending_steps { 0U };
  bool stopped { false };
  SessionState session { SessionState::kRunning };

private:
  EngineConfig config_ {};
  commands::CommandRegistry commands_;
  console::Console console_ { observer_ptr { &commands_ } };
};

} // namespace oxygen::control::testing
