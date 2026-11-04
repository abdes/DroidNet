//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <stop_token>
#include <string_view>
#include <thread>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/StopSignal.h>

namespace oxygen::content::import::tool {

namespace {

  // The signal handler reaches only objects with static storage duration.
  // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
  std::atomic<bool> g_stop_requested { false };
  std::atomic<int> g_stop_signal { 0 };
  std::atomic<int> g_stop_count { 0 };
  // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

  constexpr int kForcedExitSignalCount = 3;
  constexpr int kSignalExitBase = 128;

  auto HandleStopSignal(const int signal) -> void
  {
    // The Windows runtime restores the default action before it calls a
    // handler, so without this the next signal would end the process uncounted.
    std::signal(signal, HandleStopSignal);
    const int count = g_stop_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (count == 1) {
      g_stop_signal.store(signal, std::memory_order_release);
      g_stop_requested.store(true, std::memory_order_release);
    }
    if (count >= kForcedExitSignalCount) {
      std::_Exit(kSignalExitBase + signal);
    }
  }

  auto StopReasonMessage(const int signal) -> std::string_view
  {
    switch (signal) {
    case SIGINT:
      return "Stopping: interrupted (SIGINT)";
    case SIGTERM:
      return "Stopping: terminated (SIGTERM)";
#ifdef SIGBREAK
    case SIGBREAK:
      return "Stopping: interrupted (SIGBREAK)";
#endif
    default:
      return "Stopping: interrupted";
    }
  }

  constexpr auto kStopPollInterval = std::chrono::milliseconds(50);

} // namespace

auto InstallStopHandlers() -> void
{
  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);
#ifdef SIGBREAK
  std::signal(SIGBREAK, HandleStopSignal);
#endif
}

auto StartStopWatcher(AsyncImportService* service,
  oxygen::observer_ptr<IMessageWriter> writer) -> std::jthread
{
  return std::jthread([service, writer](const std::stop_token& st) -> void {
    while (!st.stop_requested()) {
      if (g_stop_requested.load(std::memory_order_acquire)) {
        if (service != nullptr) {
          if (writer != nullptr) {
            writer->Report(
              StopReasonMessage(g_stop_signal.load(std::memory_order_acquire)));
          }
          service->CancelAll();
          service->RequestShutdown();
        }
        break;
      }
      std::this_thread::sleep_for(kStopPollInterval);
    }
  });
}

} // namespace oxygen::content::import::tool
