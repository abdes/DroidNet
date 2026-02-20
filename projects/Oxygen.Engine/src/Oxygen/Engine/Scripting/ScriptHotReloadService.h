//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen {
class AsyncEngine;
}

namespace oxygen::scripting {

//! Background service that monitors script source directories for changes.
/*!
  The ScriptHotReloadService is an engine-level service that monitors all
  registered script source roots (provided via PathFinder) for modifications
   to .lua and .luau files.

  When a change is detected, it triggers a reload request via the AssetLoader,
  which maps the disk change back to a specific asset and notifies subscribers.

  ### Design Contracts
  - **Asynchronous**: Runs entirely in a background coroutine loop (WatchLoop).
  - **Multi-Root**: Automatically watches Engine, Game, and Addon roots.
  - **Debounced**: Includes a small delay after detection to ensure the OS
    has finished writing the file before triggering a reload.
*/
class ScriptHotReloadService final : public co::LiveObject {
public:
  //! Function signature for reload notifications.
  using ReloadCallback = std::function<void(const std::filesystem::path&)>;

  //! Constructs the service with its dependencies.
  //! @param engine The parent engine instance.
  //! @param path_finder Used to resolve the list of source roots to watch.
  //! @param on_reload Callback triggered when a file change is detected.
  //! @param poll_interval How frequently to scan the filesystem.
  OXGN_NGIN_API explicit ScriptHotReloadService(AsyncEngine& engine,
    PathFinder path_finder, ReloadCallback on_reload,
    std::chrono::milliseconds poll_interval);

  OXGN_NGIN_API ~ScriptHotReloadService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptHotReloadService)
  OXYGEN_DEFAULT_MOVABLE(ScriptHotReloadService)

  OXGN_NGIN_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;

  OXGN_NGIN_API void Run() override;
  OXGN_NGIN_API void Stop() override;
  OXGN_NGIN_NDAPI auto IsRunning() const -> bool override;

private:
  auto WatchLoop() -> co::Co<>;

  observer_ptr<AsyncEngine> engine_;
  PathFinder path_finder_;
  ReloadCallback on_reload_;
  std::chrono::milliseconds poll_interval_;
  co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen::scripting
