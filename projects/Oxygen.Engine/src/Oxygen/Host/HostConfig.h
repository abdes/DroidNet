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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Queues.h>

namespace oxygen {
class AsyncEngine;
class Graphics;
class Platform;
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

  graphics::SharedTransferQueueStrategy queue_strategy;

  std::shared_ptr<Platform> platform;
  std::weak_ptr<Graphics> gfx_weak;
  std::shared_ptr<AsyncEngine> engine;

  //! Registered by `RunHost` before any application module.
  observer_ptr<engine::InputSystem> input_system;

  //! Cleared to stop the host event loop.
  std::atomic_bool running { false };
};

} // namespace oxygen::host
