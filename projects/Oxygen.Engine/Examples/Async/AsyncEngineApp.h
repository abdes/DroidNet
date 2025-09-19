//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Queues.h> // SharedTransferQueueStrategy

namespace oxygen {

class Platform;
class Graphics;
class AsyncEngine; // Note: AsyncEngine lives in namespace oxygen

namespace engine {
  class InputSystem;
  class Renderer;
} // namespace engine

namespace examples::async {

  //! Aggregated application state used by the async example event loop.
  /*! Holds platform, graphics, engine, and module pointers shared across
   the async example. Modules can inspect immutable configuration (e.g.,
   fullscreen/headless) and observe engine subsystems via observer_ptr.
  */
  struct AsyncEngineApp {
    bool headless { false };
    bool fullscreen { false };

    // Graphics queues setup shared across subsystems
    oxygen::graphics::SharedTransferQueueStrategy queue_strategy;

    // Core systems
    std::shared_ptr<oxygen::Platform> platform;
    std::weak_ptr<oxygen::Graphics> gfx_weak;
    std::shared_ptr<oxygen::AsyncEngine> engine;

    // Observed modules (non-owning)
    observer_ptr<oxygen::engine::Renderer> renderer { nullptr };
    observer_ptr<oxygen::engine::InputSystem> input_system { nullptr };

    //! Flag toggled to request loop continue/stop
    std::atomic_bool running { false };
  };

} // namespace examples::async
} // namespace oxygen
