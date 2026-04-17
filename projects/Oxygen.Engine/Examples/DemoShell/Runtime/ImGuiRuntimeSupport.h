//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

class AsyncEngine;
class Platform;

namespace engine {
struct ModuleEvent;
}

namespace examples {

auto CreateImGuiRuntimeModule(const std::shared_ptr<Platform>& platform)
  -> std::unique_ptr<engine::EngineModule>;

auto AttachImGuiWindow(
  observer_ptr<AsyncEngine> engine, platform::WindowIdType window_id) -> bool;

auto DetachImGuiWindow(observer_ptr<AsyncEngine> engine) noexcept -> void;

[[nodiscard]] auto IsImGuiRuntimeModuleEvent(
  const engine::ModuleEvent& event) noexcept -> bool;

} // namespace examples

} // namespace oxygen
