//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <csignal>
#include <system_error>
#include <type_traits>

#include <asio/signal_set.hpp>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::platform::AsyncOps;

auto AsyncOps::HandleSignal(const std::error_code& error, int signal_number)
  -> void
{
  if (error) {
    LOG_F(ERROR, "Signal handler error: {}", error.message());
    return;
  }

  switch (signal_number) {
  case SIGINT:
    LOG_F(INFO, "Received SIGINT");
    break;

  case SIGTERM:
    LOG_F(INFO, "Received SIGTERM");
    break;

  default:
    LOG_F(1, "Received signal `{}` (unhandled)", signal_number);
    signals_.async_wait(
      [this](const std::error_code& error, int signal_number) {
        HandleSignal(error, signal_number);
      });
    return;
  }

  // Trigger the termination event
  terminate_.Trigger();
}

AsyncOps::AsyncOps(const PlatformConfig& config)
  : io_()
  , signals_(io_, SIGINT, SIGTERM)
{
  if (config.thread_pool_size > 0) {
    threads_ = std::make_unique<co::ThreadPool>(io_,
      std::max(config.thread_pool_size, std::thread::hardware_concurrency()));
    LOG_F(INFO, "Thread pool created (size={})", config.thread_pool_size);
  }
}

AsyncOps::~AsyncOps()
{
  DLOG_IF_F(WARNING, (nursery_ != nullptr),
    "LiveObject destructor called while nursery is still open. "
    "Did you forget to call Stop() on the LiveObject?");

  threads_.reset();
  io_.stop();
}

auto AsyncOps::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  signals_.async_wait([this](const std::error_code& error, int signal_number) {
    HandleSignal(error, signal_number);
  });

  return OpenNursery(nursery_, std::move(started));
}

auto AsyncOps::Stop() -> void
{
  io_.stop();
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto AsyncOps::PollOne() -> size_t
{
  const auto result = io_.poll();
  return result;
}
