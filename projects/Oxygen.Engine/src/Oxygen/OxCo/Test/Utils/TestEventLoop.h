//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidConstOrRefDataMembers
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>
#include <Oxygen/OxCo/EventLoop.h>

using oxygen::co::detail::ScopeGuard;
using std::chrono::milliseconds;

namespace oxygen::co::testing {

struct NonCancellableTag { };
constexpr NonCancellableTag kNonCancellable;

using EventQueue = std::multimap<milliseconds, std::function<void()>>;
//! Deterministic test event loop with thread-safe scheduling.
/*!
 Minimal deterministic event loop for unit tests. Earlier (legacy) version
 stopped as soon as its queue became empty. When a `ThreadPool` posted a
 completion after that check, `co::Run()` observed an unfinished awaitable
 while the loop had already exited, triggering an abort. This implementation:
 1) Makes scheduling thread-safe, 2) Blocks waiting for new events instead of
 exiting early, and 3) Integrates cleanly with `ThreadNotification`.

 ### Core Behaviors
 - **Deterministic Time**: `now_` jumps to the next event's timestamp.
 - **Zero-Delay Batching**: Multiple 0 ms posts execute inside a single time
   slice (stable order by multimap insertion rules).
 - **Fair Ordering**: All callbacks for the same timestamp execute FIFO.
 - **Graceful Stop**: Loop exits only after `Stop()` AND queue empties.

 ### Basic Usage
 ```cpp
 using namespace std::chrono_literals;
 co::testing::TestEventLoop loop;
 co::Run(loop, [&]() -> co::Co<> {
   // Simulate work
   co_await loop.Sleep(5ms);
   co_await loop.Sleep(0ms); // zero-delay chain
   co_return;
 });
 ```

 ### ThreadPool Integration Example
 ```cpp
 using namespace std::chrono_literals;
 co::testing::TestEventLoop loop;
 co::ThreadPool pool(loop, 4);
 co::Run(loop, [&]() -> co::Co<> {
   int value = 0;
   // Offload blocking or CPU-bound work
   value = co_await pool.Run([] { std::this_thread::sleep_for(2ms); return 42;
 }); CHECK_F(value == 42); co_return;
 });
 ```

 ### Example Test Pattern (Timeout / Cancellation)
 ```cpp
 TEST(MyAsyncTests, SleepCancellation) {
   co::testing::TestEventLoop loop;
   co::Run(loop, [&]() -> co::Co<> {
     auto sleeper = loop.Sleep(50ms); // cancellable
     // Simulate external cancellation before it fires
     co_await loop.Sleep(0ms); // ensure scheduling interleave
     sleeper.cancel(); // (pseudo API) illustrate concept
     co_return;
   });
 }
 ```

 ### Pitfalls & Guidance
 - **Long Blocking Callbacks**: Avoid heavy work inside scheduled lambdas;
   offload to `ThreadPool` and return results via posted completion.
 - **Time Skew Assumptions**: Do not assume wall-clock progression equals
   simulated time; only event timestamps advance `now_`.
 - **Posting After Stop**: Undefined; ensure all producers finish before
   requesting `Stop()`.
 - **Data Races**: Never mutate shared test state from worker threads without
   synchronization; ThreadPool tasks run concurrently.
 - **Deadlocks**: A callback that waits on a result that requires the loop
   to process another queued event can deadlock; design tasks to be
   self-contained.

 @warning Long blocking operations inside callbacks stall all progress and
          can hide deadlocks; keep them short or offload to the pool.
 @see ThreadNotification, EventLoopTraits, ThreadPool
 */
class TestEventLoop {
public:
  //
  // Public EventLoop-like interface
  //
  //! Run loop until `Stop()` requested, processing events in time order.
  void Run()
  {
    DLOG_F(INFO, "=== running test case ===");
    {
      std::scoped_lock lk(mutex_);
      running_ = true;
    }
    for (;;) {
      std::function<void()> task;
      milliseconds task_time { 0 };
      {
        std::unique_lock lk(mutex_);
        cv_.wait(lk, [&] { return !running_ || !events_.empty(); });
        if (!running_ && events_.empty())
          break; // graceful stop
        if (events_.empty())
          continue; // spurious wake
        auto it = events_.begin();
        task_time = it->first;
        task = std::move(it->second);
        events_.erase(it);
      }
      if (now_ != task_time) {
        now_ = task_time;
        DLOG_F(1, "-- {} ms --",
          std::chrono::duration_cast<std::chrono::milliseconds>(now_).count());
      }
      task();
    }
  }
  TestEventLoop() = default;
  ~TestEventLoop()
  {
    if (!events_.empty()) {
      DLOG_F(INFO, "=== running event leftovers ===");
      while (!events_.empty()) {
        Step();
      }
    }
    DLOG_F(INFO, "=== done ===");
  }

  OXYGEN_MAKE_NON_COPYABLE(TestEventLoop)
  OXYGEN_MAKE_NON_MOVABLE(TestEventLoop)

  //! Signal loop to stop; exits once queue drained.
  void Stop()
  {
    {
      std::scoped_lock lk(mutex_);
      running_ = false;
    }
    cv_.notify_all();
  }
  [[nodiscard]] auto IsRunning() const { return running_; }

  //
  // Fixture interface visible to test bodies
  //

  [[nodiscard]] auto Now() const { return now_; }

  //! Schedule callback at simulated time `now + delay` (thread-safe).
  void Schedule(const milliseconds delay, std::function<void()> cb)
  {
    {
      std::scoped_lock lk(mutex_);
      events_.emplace(now_ + delay, std::move(cb));
    }
    cv_.notify_one();
  }

  template <bool Cancellable> class SleepAwaitable {
  public:
    SleepAwaitable(TestEventLoop& event_loop, const milliseconds delay)
      : event_loop_(event_loop)
      , delay_(delay)
    {
    }

    ~SleepAwaitable() { CHECK_F(!suspended_); }

    OXYGEN_MAKE_NON_COPYABLE(SleepAwaitable)
    OXYGEN_DEFAULT_MOVABLE(SleepAwaitable)

    // ReSharper disable CppMemberFunctionMayBeStatic
    auto await_early_cancel() noexcept
    {
      if constexpr (Cancellable) {
        DLOG_F(1, "Sleep {} ({} ms) early cancellable", fmt::ptr(this),
          DelayMilliseconds());
        return std::true_type {};
      } else {
        DLOG_F(1, "sleep {} ({} ms) NOT early cancellable", fmt::ptr(this),
          DelayMilliseconds());
        return false;
      }
    }

    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(const detail::Handle h)
    {
      DLOG_F(
        1, "    ...on sleep {} ({} ms)", fmt::ptr(this), DelayMilliseconds());
      suspended_ = true;
      parent_ = h;
      {
        std::scoped_lock lk(event_loop_.mutex_);
        it_ = event_loop_.events_.emplace(event_loop_.now_ + delay_, [this] {
          DLOG_F(1, "sleep {} ({} ms) resuming parent", fmt::ptr(this),
            DelayMilliseconds());
          suspended_ = false;
          parent_.resume();
        });
      }
      event_loop_.cv_.notify_one();
    }
    auto await_cancel(detail::Handle) noexcept
    {
      if constexpr (Cancellable) {
        LOG_F(1, "Sleep {} ({} ms) cancelling", fmt::ptr(this),
          DelayMilliseconds());
        {
          std::scoped_lock lk(event_loop_.mutex_);
          event_loop_.events_.erase(it_);
        }
        event_loop_.cv_.notify_one();
        suspended_ = false;
        return std::true_type {};
      } else {
        DLOG_F(1, "Sleep {} ({} ms) reject cancellation", fmt::ptr(this),
          DelayMilliseconds());
        return false;
      }
    }
    [[nodiscard]] auto await_must_resume() const noexcept
    {
      // shouldn't actually be called unless await_cancel() returns false
      CHECK_F(!Cancellable);
      if constexpr (Cancellable) {
        return std::false_type {};
      } else {
        return true;
      }
    }
    void await_resume() { suspended_ = false; }
    // ReSharper restore CppMemberFunctionMayBeStatic

  private:
    [[nodiscard]] auto DelayMilliseconds() const -> long
    {
      return static_cast<long>(delay_.count());
    }

    TestEventLoop& event_loop_;
    // ReSharper disable CppDFANotInitializedField (initialized by
    // SleepAwaitable)
    milliseconds delay_;
    detail::Handle parent_;
    EventQueue::iterator it_;
    bool suspended_ = false;
  };

  //! Cancellable sleep awaitable.
  /*!
   Usage:
   ```cpp
   co_await loop.Sleep(10ms);
   ```
   Guarantees ordering relative to other events scheduled before the same
   target timestamp. Cancellation (via coroutine cancellation path) erases
   the queued event if it has not yet executed.
  */
  auto Sleep(const milliseconds tm) -> Awaitable<void> auto
  {
    return SleepAwaitable<true>(*this, tm);
  }

  //! Non-cancellable sleep awaitable.
  /*!
   Behaves like `Sleep()` but refuses cancellation; used to assert that
   higher-level cancellation logic does not erroneously remove required
   waits.
  */
  auto Sleep(const milliseconds tm, NonCancellableTag) -> Awaitable<void> auto
  {
    return SleepAwaitable<false>(*this, tm);
  }

private:
  void Step() { /* no-op: legacy; kept for compatibility */ }

  EventQueue events_ {};
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool running_ { false };
  milliseconds now_ = milliseconds(0);
};

} // namespace oxygen::co::testing

template <>
struct oxygen::co::EventLoopTraits<oxygen::co::testing::TestEventLoop> {
  static auto EventLoopId(testing::TestEventLoop& p) noexcept -> EventLoopID
  {
    return EventLoopID(&p);
  }
  static void Run(testing::TestEventLoop& p) { p.Run(); }
  static void Stop(testing::TestEventLoop& p) noexcept { p.Stop(); }
  static auto IsRunning(const testing::TestEventLoop& p) noexcept -> bool
  {
    return p.IsRunning();
  }
};

static_assert(oxygen::co::detail::Cancellable<
  oxygen::co::testing::TestEventLoop::SleepAwaitable<true>>);

// ThreadPool integration for TestEventLoop
namespace oxygen::co {
template <> class ThreadNotification<testing::TestEventLoop> {
public:
  ThreadNotification(testing::TestEventLoop& loop, void (*fn)(void*), void* arg)
    : loop_(loop)
    , fn_(fn)
    , arg_(arg)
  {
  }

  //! Post a zero-delay callback onto the test event loop (thread-safe).
  /*!
   Invoked by worker threads in `co::ThreadPool` to marshal continuations
   back onto the loop thread. The callback is inserted with a 0 ms delay so
   ordering among multiple completions is FIFO with respect to their post
   sequence. This preserves deterministic test behavior even under high
   concurrency.

   ### Example (simplified)
   ```cpp
   co::testing::TestEventLoop loop;
   co::ThreadPool pool(loop, 2);
   co::Run(loop, [&]() -> co::Co<> {
     int a = co_await pool.Run([]{ return 1; });
     int b = co_await pool.Run([]{ return 2; });
     CHECK_F(a + b == 3);
     co_return;
   });
   ```

   ### Pitfalls
   - Posting after `Stop()` may enqueue tasks that never run (undefined).
   - Heavy callbacks here still execute on loop thread; keep them light.
  */
  void Post(testing::TestEventLoop& loop, void (*fn)(void*), void* arg) noexcept
  {
    loop.Schedule(std::chrono::milliseconds(0), [fn, arg] { fn(arg); });
  }

private:
  testing::TestEventLoop& loop_;
  void (*fn_)(void*);
  void* arg_;
};
} // namespace oxygen::co
