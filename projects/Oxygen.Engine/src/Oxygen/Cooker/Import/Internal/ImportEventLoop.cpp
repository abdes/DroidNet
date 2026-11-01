//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <system_error>
#include <thread>
#include <utility>

#include "ImportEventLoop.h"
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

namespace {

  //! The driver of `context`; the OS error when its backend cannot be created.
  auto CreateDriver(asio::io_context& context) -> serio::IoDriver
  {
    auto driver = serio::IoDriver::Create(context);
    if (!driver) {
      throw std::system_error(
        driver.error(), "Failed to create the import IO driver");
    }
    return std::move(driver).value();
  }

} // namespace

ImportEventLoop::ImportEventLoop()
  : driver_(CreateDriver(io_context_))
{
  DLOG_F(INFO, "Created");
}

ImportEventLoop::~ImportEventLoop()
{
  if (running_.load(std::memory_order_acquire)) {
    try {
      Stop();
    } catch (const std::system_error& error) {
      ABORT_F("Failed to stop import event loop during destruction: {}",
        error.what());
    }
  }
  DLOG_F(INFO, "Destroyed");
}

auto ImportEventLoop::IoContext() noexcept -> asio::io_context&
{
  return io_context_;
}

auto ImportEventLoop::IoContext() const noexcept -> const asio::io_context&
{
  return io_context_;
}

auto ImportEventLoop::Driver() noexcept -> serio::IoDriver& { return driver_; }

auto ImportEventLoop::Run() -> void
{
  DCHECK_F(!running_.load(std::memory_order_acquire),
    "ImportEventLoop::Run() called while already running");

  running_.store(true, std::memory_order_release);
  running_thread_id_ = std::this_thread::get_id();

  DLOG_F(INFO, "Run starting");

  // Every run needs its own guard: a restarted context may initially be
  // waiting only for a ThreadPool completion that has not been posted yet.
  auto work_guard = asio::make_work_guard(io_context_);
  // Run until Stop() is called
  io_context_.run();

  // Drop outstanding work before restart, since releasing the last guard can
  // stop the context again. Stop() itself only touches thread-safe ASIO state.
  work_guard.reset();
  // Reset for potential reuse
  io_context_.restart();
  running_.store(false, std::memory_order_release);
  running_thread_id_ = std::thread::id {};

  DLOG_F(INFO, "Run exited");
}

auto ImportEventLoop::Stop() -> void
{
  DLOG_F(INFO, "Stop called");

  // Stop the io_context immediately
  io_context_.stop();
}

auto ImportEventLoop::IsRunning() const noexcept -> bool
{
  return running_.load(std::memory_order_acquire)
    && std::this_thread::get_id() == running_thread_id_;
}

} // namespace oxygen::content::import
