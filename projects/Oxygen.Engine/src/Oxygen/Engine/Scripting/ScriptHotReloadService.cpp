//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/ScriptHotReloadService.h>
#include <Oxygen/Platform/Platform.h>

namespace oxygen::scripting {

ScriptHotReloadService::ScriptHotReloadService(AsyncEngine& engine,
  PathFinder path_finder, ReloadCallback on_reload,
  const std::chrono::milliseconds poll_interval)
  : engine_(&engine)
  , path_finder_(std::move(path_finder))
  , on_reload_(std::move(on_reload))
  , poll_interval_(poll_interval)
{
}

auto ScriptHotReloadService::ActivateAsync(co::TaskStarted<> started)
  -> co::Co<>
{
  return co::OpenNursery(nursery_, std::move(started));
}

void ScriptHotReloadService::Run()
{
  if (nursery_ != nullptr) {
    nursery_->Start(&ScriptHotReloadService::WatchLoop, this);
  }
}

void ScriptHotReloadService::Stop()
{
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto ScriptHotReloadService::IsRunning() const -> bool
{
  return nursery_ != nullptr;
}

auto ScriptHotReloadService::SetEnabled(const bool enabled) noexcept -> void
{
  enabled_.store(enabled, std::memory_order_release);
}

auto ScriptHotReloadService::SetPollInterval(
  const std::chrono::milliseconds interval) noexcept -> void
{
  poll_interval_.store(interval, std::memory_order_release);
}

auto ScriptHotReloadService::WatchLoop() -> co::Co<>
{
  const auto roots = path_finder_.ScriptSourceRoots();
  if (roots.empty()) {
    LOG_F(WARNING, "no script source roots registered; service idle");
    co_return;
  }

  auto is_script = [](const std::filesystem::path& path) {
    const auto ext = path.extension();
    return ext == ".luau" || ext == ".lua";
  };

  std::unordered_map<std::string, std::filesystem::file_time_type>
    last_write_times;

  // Initial scan of all roots
  for (const auto& root : roots) {
    LOG_F(INFO, "starting watch on {}", root.generic_string());
    if (!std::filesystem::exists(root)) {
      LOG_F(WARNING, "root does not exist: {}", root.string());
      continue;
    }

    for (const auto& entry :
      std::filesystem::recursive_directory_iterator(root)) {
      if (entry.is_regular_file() && is_script(entry.path())) {
        std::error_code ec;
        last_write_times[entry.path().generic_string()]
          = entry.last_write_time(ec);
      }
    }
  }

  while (true) {
    co_await engine_->GetPlatform().Async().SleepFor(
      poll_interval_.load(std::memory_order_acquire));

    if (!enabled_.load(std::memory_order_acquire)) {
      continue;
    }

    for (const auto& root : roots) {
      if (!std::filesystem::exists(root)) {
        continue;
      }

      for (const auto& entry :
        std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !is_script(entry.path())) {
          continue;
        }

        std::error_code ec;
        const auto current_time = entry.last_write_time(ec);
        if (ec) {
          continue;
        }

        const auto path_str = entry.path().generic_string();
        const auto it = last_write_times.find(path_str);
        if (it == last_write_times.end()) {
          last_write_times[path_str] = current_time;
        } else if (it->second != current_time) {
          last_write_times[path_str] = current_time;
          LOG_F(INFO, "detected change in {}", path_str);
          if (on_reload_) {
            // Debounce: wait a bit to ensure file is fully written/flushed
            co_await engine_->GetPlatform().Async().SleepFor(
              std::chrono::milliseconds(100)); // NOLINT
            on_reload_(entry.path());
          }
        }
      }
    }
  }
}

} // namespace oxygen::scripting
