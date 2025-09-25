//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Queues.h>

namespace oxygen {

class Platform;
class Graphics;
class AsyncEngine;

namespace engine {
  class InputSystem;
  class Renderer;
} // namespace engine

} // namespace oxygen

namespace oxygen::engine::interop {

struct EngineContext {
  // Graphics queues setup shared across subsystems
  graphics::SharedTransferQueueStrategy queue_strategy;

  // Core systems
  std::shared_ptr<Platform> platform;
  std::weak_ptr<Graphics> gfx_weak;
  std::shared_ptr<AsyncEngine> engine;

  // Observed modules (non-owning), owned by the AsyncEngine ModuleManager
  observer_ptr<engine::Renderer> renderer { nullptr };
  observer_ptr<engine::InputSystem> input_system { nullptr };

  //! Flag toggled to request loop continue/stop
  std::atomic_bool running { false };
};

} // namespace oxygen::engine::interop
