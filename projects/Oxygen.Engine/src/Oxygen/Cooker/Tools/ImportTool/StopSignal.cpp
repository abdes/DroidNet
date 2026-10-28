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
  std::atomic<bool> g_stop_handled { false };
  std::atomic<int> g_stop_signal { 0 };
  std::atomic<int> g_stop_count { 0 };
  // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

  auto HandleStopSignal(const int signal) -> void
  {
    const int count = g_stop_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (count == 1) {
      g_stop_requested.store(true, std::memory_order_relaxed);
      int expected = 0;
      g_stop_signal.compare_exchange_strong(
        expected, signal, std::memory_order_acq_rel);
      std::signal(SIGINT, HandleStopSignal);
      std::signal(SIGTERM, HandleStopSignal);
      return;
    }
    if (count >= 3) {
      const int exit_code = (signal == SIGTERM) ? 143 : 130;
      std::_Exit(exit_code);
    }
  }

  auto StopReasonMessage(const int signal) -> std::string_view
  {
    switch (signal) {
    case SIGINT:
      return "Stopping: interrupted (SIGINT)";
    case SIGTERM:
      return "Stopping: terminated (SIGTERM)";
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
}

auto ResetStopState() -> void
{
  g_stop_requested.store(false, std::memory_order_relaxed);
  g_stop_handled.store(false, std::memory_order_relaxed);
  g_stop_signal.store(0, std::memory_order_relaxed);
  g_stop_count.store(0, std::memory_order_relaxed);
}

auto StartStopWatcher(AsyncImportService* service,
  oxygen::observer_ptr<IMessageWriter> writer) -> std::jthread
{
  return std::jthread([service, writer](const std::stop_token& st) -> void {
    while (!st.stop_requested()) {
      if (g_stop_requested.load(std::memory_order_relaxed)) {
        if (g_stop_handled.exchange(true, std::memory_order_acq_rel)) {
          break;
        }
        if (service != nullptr) {
          if (writer != nullptr) {
            const auto message = StopReasonMessage(
              g_stop_signal.load(std::memory_order_relaxed));
            writer->Report(message);
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
