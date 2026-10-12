//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/api_export.h>

namespace oxygen {
class AsyncEngine;
namespace engine {
  class EngineModule;
} // namespace engine
} // namespace oxygen

namespace oxygen::host {

//! Registers the application's modules once the engine runs.
using RegisterModules = std::function<void(AsyncEngine&, HostContext&)>;

//! Runs the platform, graphics and engine until the engine stops.
/*!
 In order: creates the platform and loads the graphics backend; creates the
 command queues and the engine; activates and runs platform, graphics and
 engine; registers the input system, then the ImGui module unless headless,
 then the application's modules; stops the engine when the last window
 closes; tears everything down in reverse order.

 Exceptions, including those thrown by `register_modules`, are logged and
 turned into `EXIT_FAILURE`.

 \return the process exit code.
*/
OXGN_HOST_NDAPI auto RunHost(
  HostConfig config, const RegisterModules& register_modules) -> int;

//! Registers a module the application cannot run without.
/*!
 \throws std::runtime_error when the engine rejects the module.
*/
OXGN_HOST_API auto RegisterRequiredModule(
  AsyncEngine& engine, std::unique_ptr<engine::EngineModule> module) -> void;

} // namespace oxygen::host
