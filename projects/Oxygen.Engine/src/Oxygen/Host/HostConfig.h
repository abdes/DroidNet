//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Control/ControlServer.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Queues.h>

namespace oxygen {
class AsyncEngine;
class Graphics;
class Platform;
namespace co {
  class Nursery;
} // namespace co
namespace engine {
  class InputSystem;
} // namespace engine
} // namespace oxygen

namespace oxygen::host {

//! Main window options a host applies when it creates its window.
struct HostWindowConfig {
  //! Framebuffer size in pixels; none keeps the window's default size.
  std::optional<Extent<std::uint32_t>> resolution;
  bool fullscreen { false };
};

//! Everything `RunHost` needs to start the platform, graphics and engine.
/*!
 `engine.graphics` carries the graphics tooling, vsync and frame capture
 options. `platform.headless` selects a headless run: `RunHost` then uses the
 headless backend, sets `engine.graphics.headless` and creates no ImGui module.
*/
struct HostConfig {
  EngineConfig engine;
  PlatformConfig platform;
  HostWindowConfig window;
  graphics::BackendType backend { graphics::BackendType::kDirect3D12 };
  console::ConsoleStartupPlan startup_cvars;
  //! Starts the JSON-RPC control server when set.
  std::optional<control::ControlServerConfig> control;
};

//! Runtime state shared by the host and the modules it registers.
/*!
 Filled by `RunHost` before it calls the application's module registration.
 The platform and the engine outlive every module; graphics is held weakly
 because the backend can be unloaded.
*/
struct HostContext {
  bool headless { false };
  HostWindowConfig window;
  //! The control server's launch label; shown in the window title so a
  //! person can tell instances apart. Empty without one.
  std::string instance_name;
  //! False when another instance of the application owns its saved state;
  //! this instance then must not save settings of its own.
  bool owns_persistence { true };

  graphics::SharedTransferQueueStrategy queue_strategy;

  std::shared_ptr<Platform> platform;
  std::weak_ptr<Graphics> gfx_weak;
  std::shared_ptr<AsyncEngine> engine;

  //! Registered by `RunHost` before any application module.
  observer_ptr<engine::InputSystem> input_system;

  //! Events for remote clients; modules publish their own topics here.
  std::unique_ptr<control::EventLog> events
    = std::make_unique<control::EventLog>();
  //! Runs every command invocation; created and activated by `RunHost`
  //! before any application module is registered.
  std::unique_ptr<control::CommandDispatcher> commands;
  //! The control server, when `HostConfig::control` asks for one.
  std::unique_ptr<control::ControlServer> control_server;

  //! Runs background tasks on the engine thread until the engine completes;
  //! tasks still running then are cancelled. Set before any application
  //! module is registered.
  observer_ptr<co::Nursery> nursery;

  //! Cleared to stop the host event loop.
  std::atomic_bool running { false };
};

} // namespace oxygen::host
