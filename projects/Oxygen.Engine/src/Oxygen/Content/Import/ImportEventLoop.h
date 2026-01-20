//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/EventLoop.h>

namespace oxygen::content::import {

//! ASIO-based event loop for the dedicated import thread.
/*!
 ImportEventLoop wraps an ASIO io_context specifically for async import
 operations. It runs on a dedicated thread, separate from the main application
 event loop.

 ### Key Features

 - **Dedicated thread**: Runs independently of the main application.
 - **ASIO integration**: Uses asio::io_context for async operations.
 - **Work guard**: Prevents premature exit when no work is pending.
 - **Thread-safe stop**: Can be stopped from any thread.

 ### Usage Patterns

 This class is internal to the async import system. External code should use
 `AsyncImportService` instead.

 @see AsyncImportService, co::ThreadPool
*/
class ImportEventLoop {
public:
  //! Construct the event loop. Does not start running.
  OXGN_CNTT_API ImportEventLoop();

  //! Destructor. Stops the event loop if running.
  OXGN_CNTT_API ~ImportEventLoop();

  OXYGEN_MAKE_NON_COPYABLE(ImportEventLoop)
  OXYGEN_MAKE_NON_MOVABLE(ImportEventLoop)

  //! Get the underlying ASIO io_context.
  OXGN_CNTT_NDAPI auto IoContext() noexcept -> asio::io_context&;

  //! Get the underlying ASIO io_context (const).
  OXGN_CNTT_NDAPI auto IoContext() const noexcept -> const asio::io_context&;

  //! Run the event loop. Blocks until Stop() is called.
  /*!
   This should be called from the import thread. It will process events
   until Stop() is called from any thread.
  */
  OXGN_CNTT_API auto Run() -> void;

  //! Request the event loop to stop. Thread-safe.
  /*!
   Can be called from any thread. The event loop will exit its Run()
   method shortly after this is called.
  */
  OXGN_CNTT_API auto Stop() -> void;

  //! Check if the event loop is currently running.
  OXGN_CNTT_NDAPI auto IsRunning() const noexcept -> bool;

  //! Post a callback to run on this event loop. Thread-safe.
  /*!
   The callback will be executed on the import thread during the next
   iteration of the event loop.

   @param fn The callback to execute.
  */
  template <typename F> auto Post(F&& fn) -> void
  {
    asio::post(io_context_, std::forward<F>(fn));
  }

private:
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::atomic<bool> running_ { false };
  std::thread::id running_thread_id_ {};
};

} // namespace oxygen::content::import

//=== EventLoopTraits Specialization
//===-----------------------------------------//

namespace oxygen::co {

//! EventLoopTraits specialization for ImportEventLoop.
template <> struct EventLoopTraits<content::import::ImportEventLoop> {
  static auto EventLoopId(content::import::ImportEventLoop& loop) -> EventLoopID
  {
    return EventLoopID(&loop.IoContext());
  }

  static void Run(content::import::ImportEventLoop& loop) { loop.Run(); }

  static void Stop(content::import::ImportEventLoop& loop) { loop.Stop(); }

  static auto IsRunning(content::import::ImportEventLoop& loop) noexcept -> bool
  {
    return loop.IsRunning();
  }
};

//! ThreadNotification specialization for ImportEventLoop.
/*!
 Enables co::ThreadPool to post completion callbacks back to the import
 thread's event loop.
*/
template <> class ThreadNotification<content::import::ImportEventLoop> {
public:
  //! Construct the notification. The fn/arg are stored for later use.
  ThreadNotification(content::import::ImportEventLoop& /*loop*/,
    void (*)(void*) /*fn*/, void* /*arg*/)
  {
    // Stateless implementation - we capture fn/arg at Post time
  }

  //! Post a callback to the import event loop. Thread-safe.
  void Post(content::import::ImportEventLoop& loop, void (*fn)(void*),
    void* arg) noexcept
  {
    loop.Post([fn, arg]() { fn(arg); });
  }
};

} // namespace oxygen::co
