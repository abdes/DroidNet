//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Queues.h> // SharedTransferQueueStrategy

namespace oxygen {

class Platform;
class Graphics;
class AsyncEngine; // Note: AsyncEngine lives in namespace oxygen
} // namespace oxygen

namespace oxygen::engine {
class InputSystem;
class Renderer;
} // namespace oxygen::engine

namespace oxygen::examples::common {

//! Aggregated application state used by the async example event loop.
/*! Holds platform, graphics, engine, and module pointers shared across
 async examples. Modules can inspect immutable configuration (e.g.,
 fullscreen/headless) and observe engine subsystems via observer_ptr.
*/
struct AsyncEngineApp {
  bool headless { false };
  bool fullscreen { false };

  //! Workspace root used for path resolution.
  std::filesystem::path workspace_root {};

  // Graphics queues setup shared across subsystems
  oxygen::graphics::SharedTransferQueueStrategy queue_strategy;

  // Core systems
  std::shared_ptr<oxygen::Platform> platform;
  std::weak_ptr<oxygen::Graphics> gfx_weak;
  std::shared_ptr<oxygen::AsyncEngine> engine;

  // Observed modules (non-owning)
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer { nullptr };
  oxygen::observer_ptr<oxygen::engine::InputSystem> input_system { nullptr };

  //! Flag toggled to request loop continue/stop
  std::atomic_bool running { false };
};

} // namespace oxygen::examples::common
