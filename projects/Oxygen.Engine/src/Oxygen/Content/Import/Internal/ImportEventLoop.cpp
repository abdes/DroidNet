//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ImportEventLoop.h"

#include <Oxygen/Base/Logging.h>

namespace oxygen::content::import {

ImportEventLoop::ImportEventLoop()
  : io_context_()
  , work_guard_(asio::make_work_guard(io_context_))
{
  DLOG_F(INFO, "ImportEventLoop created");
}

ImportEventLoop::~ImportEventLoop()
{
  if (running_.load(std::memory_order_acquire)) {
    Stop();
  }
  DLOG_F(INFO, "ImportEventLoop destroyed");
}

auto ImportEventLoop::IoContext() noexcept -> asio::io_context&
{
  return io_context_;
}

auto ImportEventLoop::IoContext() const noexcept -> const asio::io_context&
{
  return io_context_;
}

auto ImportEventLoop::Run() -> void
{
  DCHECK_F(!running_.load(std::memory_order_acquire),
    "ImportEventLoop::Run() called while already running");

  running_.store(true, std::memory_order_release);
  running_thread_id_ = std::this_thread::get_id();

  DLOG_F(INFO, "ImportEventLoop::Run() starting");

  // Run until Stop() is called
  io_context_.run();

  // Reset for potential reuse
  io_context_.restart();
  running_.store(false, std::memory_order_release);
  running_thread_id_ = std::thread::id {};

  DLOG_F(INFO, "ImportEventLoop::Run() exited");
}

auto ImportEventLoop::Stop() -> void
{
  DLOG_F(INFO, "ImportEventLoop::Stop() called");

  // Release the work guard to allow run() to exit
  work_guard_.reset();

  // Stop the io_context immediately
  io_context_.stop();
}

auto ImportEventLoop::IsRunning() const noexcept -> bool
{
  return running_.load(std::memory_order_acquire)
    && std::this_thread::get_id() == running_thread_id_;
}

} // namespace oxygen::content::import
