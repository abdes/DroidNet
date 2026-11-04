//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/ImportTool/StopSignal.cpp

// The stop state lasts for the process, so each test raises its signals in a
// process of its own.

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/StopSignal.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace tool = oxygen::content::import::tool;
using oxygen::content::import::AsyncImportService;

constexpr int kStopped = 0;
constexpr int kNotStopped = 1;

//! Raises `signal` `Count` times, then ends the process.
template <int Count> [[noreturn]] auto RaiseThenExit(const int signal) -> void
{
  tool::InstallStopHandlers();
  for (int raised = 0; raised < Count; ++raised) {
    std::raise(signal);
  }
  std::_Exit(kNotStopped);
}

//! A signal raised before the service exists, then whether the watcher stops
//! it.
[[noreturn]] auto SignalThenStartService(const int signal) -> void
{
  tool::InstallStopHandlers();
  std::raise(signal);

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 1U,
    .max_in_flight_jobs = 1U,
  });
  const auto watcher = tool::StartStopWatcher(
    &service, oxygen::observer_ptr<tool::IMessageWriter> {});
  const auto deadline
    = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (
    service.IsAcceptingJobs() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::_Exit(service.IsAcceptingJobs() ? kNotStopped : kStopped);
}

//! A stop request made before the session began still stops it.
NOLINT_TEST(StopSignalDeathTest, ASignalBeforeTheServiceStopsTheService)
{
  EXPECT_EXIT(
    SignalThenStartService(SIGINT), ::testing::ExitedWithCode(kStopped), "");
}

//! Two signals leave the run to its stop; only the third ends the process.
NOLINT_TEST(StopSignalDeathTest, TwoSignalsLeaveTheProcessRunning)
{
  EXPECT_EXIT(
    RaiseThenExit<2>(SIGINT), ::testing::ExitedWithCode(kNotStopped), "");
}

//! The third signal ends a run that does not stop, with 128 plus the signal.
NOLINT_TEST(StopSignalDeathTest, TheThirdSignalEndsTheProcess)
{
  EXPECT_EXIT(RaiseThenExit<3>(SIGINT), ::testing::ExitedWithCode(130), "");
  EXPECT_EXIT(RaiseThenExit<3>(SIGTERM), ::testing::ExitedWithCode(143), "");
}

#ifdef SIGBREAK
//! Ctrl+Break is a stop request like Ctrl+C.
NOLINT_TEST(StopSignalDeathTest, BreakIsAStopRequest)
{
  EXPECT_EXIT(
    SignalThenStartService(SIGBREAK), ::testing::ExitedWithCode(kStopped), "");
  EXPECT_EXIT(RaiseThenExit<3>(SIGBREAK), ::testing::ExitedWithCode(149), "");
}
#endif

} // namespace
