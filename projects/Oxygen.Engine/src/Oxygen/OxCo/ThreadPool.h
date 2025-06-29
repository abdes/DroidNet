//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/IntrusivePtr.h>
#include <Oxygen/OxCo/Detail/Result.h>

namespace oxygen::co {

namespace detail {
  inline auto SpinLoopBody() -> void
  {
#if defined(_MSC_VER)
    _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("pause");
#elif defined(__aarch64__)
    __asm__ __volatile__("isb");
#elif defined(__arm__)
    __asm__ __volatile__("yield");
#endif
  }
} // namespace detail

//! Thread pool for running CPU-bound tasks asynchronously.
/*!
 ThreadPool enables offloading CPU-intensive work from the main event loop to a
 pool of worker threads. It is designed for use cases where synchronous
 functions need to be executed without blocking the main thread, such as
 hashing, compression, or other heavy computations.

 ### Key Features

 - **CPU-bound task offloading**: Run synchronous functions on a thread pool.
 - **Coroutine integration**: Returns awaitables for use with coroutines.
 - **Cancellation support**: Optional CancelToken for cooperative cancellation.
 - **Custom event loop notification**: Integrates with any event loop via
   ThreadNotification specialization.

 ### Usage Patterns

 - Use for CPU-bound work, not for blocking I/O.
 - Submit tasks using `co_await threadPool.Run(fn, args...)`.
 - For cancellation, accept a `ThreadPool::CancelToken` as the last argument in
   your function and check it periodically.

 ### Usage Example
 ```cpp
 asio::io_service io;
 co::ThreadPool tp(io, 4);

 // Synchronous function to run in the pool
 int compute(int x) { return x * x; }

 co::Task<void> Example()
 {
   int result = co_await tp.Run(compute, 42);
   // result == 1764
 }

 // With cancellation support
 co::Task<void> ExampleWithCancel(std::span<const char> data)
 {
   auto hash = co_await tp.Run([&](co::ThreadPool::CancelToken cancelled) {
     int sum = 0;
     for (char c : data) {
       if (cancelled)
         break;
       sum += c;
     }
     return sum;
   });
   // ...
 }
 ```

 ### Architecture Notes

 - All public methods must be called from the main event loop thread.
 - Requires a ThreadNotification specialization for your event loop.
 - Awaitables returned by Run() are not forcibly cancellable; cancellation is
   cooperative via CancelToken.

 @see ThreadPool::Run, ThreadPool::CancelToken
 @warning Not intended for blocking I/O; may deadlock if all threads block.
*/
class ThreadPool {
  enum CancelState : uint8_t;

public:
  /// A representation of the cancellation status of a call to
  /// `ThreadPool.run()`, that can be tested from within the task that's
  /// running in the thread pool.
  class CancelToken {
    std::atomic<CancelState>* state_;
    friend class ThreadPool;

  public:
    /// Test whether cancellation has been requested. Once `true` is
    /// returned here, the cancellation is considered to have been taken:
    /// the corresponding call to `ThreadPool.run()` will terminate by
    /// propagating cancellation, and any value or error returned from
    /// the task will be ignored. So, don't check for cancellation until
    /// you're prepared to act on it. You may check for cancellation from
    /// any thread, as long as all accesses to the CancelToken are
    /// sequenced-before the task that received it completes.
    explicit operator bool() const noexcept;

    /// Allows to query for the cancellation status, but not mark
    /// the cancellation taken.
    [[nodiscard]] auto Peek() const noexcept -> bool;

    /// Marks the cancellation as taken.
    /// No-op if the cancellation was not requested or already consumed.
    auto Consume() const noexcept -> void;
  };

  /// Constructor.
  /// Requires a specialization of `corral::ThreadNotification`
  /// to be defined for the event loop (see EventLoop.h for details).
  template <class EventLoopT> ThreadPool(EventLoopT& el, unsigned thread_count);

  /// Shuts down the thread pool.
  /// UB if there are any pending or in-progress tasks.
  ~ThreadPool();

  OXYGEN_MAKE_NON_COPYABLE(ThreadPool)
  OXYGEN_DEFAULT_MOVABLE(ThreadPool)

  /*!
   Submits a task to the thread pool and suspends the current coroutine until
   the task completes, delivering the result or reraising any exceptions.

   `fn` may optionally accept a `CancelToken` as its last argument to
   periodically check if cancellation of the calling coroutine has been
   requested, and wrap up early if so. Querying the token for the cancellation
   status counts as confirming the cancellation request; any returned value (or
   exception) will be discarded.

   Any references passed as `args` are *not* decay-copied, which is fine in
   typical use cases (`co_await threadPool.run(fn, args...)` -- iow, having
   run() and co_await in the same full expression). However, if returning an
   un-awaited task from a function (e.g. `Awaitable auto foo() { return
   threadPool.run(fn, args...); }`), pay attention to the lifetime of the
   arguments.

   @tparam F The callable type to execute in the thread pool.
   @tparam Args Argument types to pass to the callable.

   @param f The function or callable object to execute.
   @param args Arguments to forward to the callable.
   @return An awaitable that yields the result of the callable or propagates any
   exception thrown.

   @warning Do not use for blocking I/O; may deadlock if all threads are busy.
   @see ThreadPool::CancelToken, ThreadPool
  */
  template <class F, class... Args>
    requires(std::invocable<F, Args...>
      // ReSharper disable once CppRedundantQualifier - Will break MSVC
      || std::invocable<F, Args..., ThreadPool::CancelToken>)
  auto Run(F&& f, Args&&... args) -> Awaitable auto;

private:
  class Task;
  template <class F, class... Args> class TaskImpl;

  struct IThreadNotification;
  template <class EventLoopT> struct ThreadNotificationImpl;

  // Main function of each worker thread.
  auto ThreadBody() -> void;

  // Runs in main thread to trampoline to `processCQ()`. `arg` is this
  // ThreadPool.
  static auto Tick(void* arg) -> void;

  // Runs in main thread to post a new task, which can be immediately serviced
  // by a worker thread. Returns false if there was no room.
  auto PushToSubmitQueue(Task* task) -> bool;

  // Runs in a worker thread to obtain a task to run, blocking if necessary.
  // Never returns null, but may return ExitRequest rather than a valid task
  // if the worker thread should terminate.
  auto PopFromSubmitQueue() -> Task*;

  // Runs in main thread to record that a new task should be posted once some
  // already-posted tasks have completed to free up submission queue space.
  auto PushToBackoffSubmitQueue(Task* task) -> void;

  // Runs in main thread to call pushToSubmitQueue() for one or more tasks
  // previously passed to pushToBackoffSubmitQueue().
  auto SubmitBackoffSubmitQueue() -> void;

  // Runs in worker thread to post the given task as completed so the main
  // thread can pick it up.
  auto PushToCompletionQueue(Task* task) -> void;

  // Runs in main thread to process task completions and wake up their
  // awaiters.
  auto DrainCompletionQueue() -> void;

  static constexpr uintptr_t kExitRequest = 1;
  static constexpr size_t kStride = 7;

  struct Slot {
    // Task to run; nullptr means this slot is free.
    //
    // Main thread checks for null using acquire ordering and then writes
    // a non-null value using release ordering. Worker thread reads the
    // value using acquire ordering.
    std::atomic<Task*> task { nullptr };

    // If true, there is a worker thread waiting to service a task in this
    // slot, so it should be woken up once a task has been written.
    // Worker thread writes true/false using release ordering. Main thread
    // checks the value using acquire ordering.
    std::atomic<bool> dequeuing;
  };

  struct Data : detail::RefCounted<Data> {
    // List of worker threads. Only accessed by main thread.
    std::vector<std::thread> threads;

    // Submission queue entries. Each worker thread claims a particular
    // entry using `sqHead.fetch_add(Stride)`, then waits for that entry
    // to be populated.
    std::unique_ptr<Slot[]> sq;

    // Index of the first slot that has not been claimed by a worker thread
    // yet. All accesses use fetch_add() to ensure exactly one thread claims
    // each index. Ever-increasing; each thread individually wraps the
    // claimed index around `sqCapacity` to obtain the actual index
    // into `sq`.
    std::atomic<size_t> sq_head = 0;

    // Index of the first slot that has not been written yet. Accessed only
    // from the main thread. Ever-increasing for uniformity with `sqHead`.
    size_t sq_tail = 0;

    // Number of slots in `sq`. Read-only after construction, so doesn't
    // need to be atomic.
    size_t sq_capacity;

    // Number of times a worker thread should spin seeking a new task to run
    // before going to sleep. Adjusted adaptively by worker threads using
    // relaxed ordering.
    std::atomic<uint32_t> sq_spin_cutoff = 200;

    // Head and tail of a linked list (linked via Task::next_) of tasks that
    // have not yet been submitted due to lack of room. Accessed only from
    // main thread.
    Task* backoff_sq_head = nullptr;
    Task** backoff_sq_tail_ptr = &backoff_sq_head;

    // Head of a linked list (linked via Task::next_) of tasks that have
    // been completed. Worker threads prepend new entries; the main thread
    // takes the whole batch and then processes them.
    std::atomic<Task*> cq_head { nullptr };

    // Interface allowing worker threads to enqueue a callback that will
    // run on the main thread. The callback calls `tick()` on the
    // ThreadPool.
    std::unique_ptr<IThreadNotification> notification;
  };
  detail::IntrusivePtr<Data> d_;
};

//
// Implementation
//

// Task

enum ThreadPool::CancelState : uint8_t { None, Requested, Confirmed };

inline ThreadPool::CancelToken::operator bool() const noexcept
{
  CancelState st = Requested;
  return state_->compare_exchange_strong(
           st, Confirmed, std::memory_order_acq_rel)
    || st == Confirmed;
}

inline auto ThreadPool::CancelToken::Peek() const noexcept -> bool
{
  const CancelState st = state_->load(std::memory_order_acquire);
  return st == Requested || st == Confirmed;
}

inline auto ThreadPool::CancelToken::Consume() const noexcept -> void
{
  CancelState st = Requested;
  state_->compare_exchange_strong(st, Confirmed, std::memory_order_release);
}

class ThreadPool::Task {
  explicit Task(ThreadPool* pool)
    : pool_(pool)
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(Task)
  OXYGEN_DEFAULT_MOVABLE(Task)

  virtual auto Run() -> void = 0;

protected:
  ~Task() = default;
  detail::Handle parent_;

private:
  ThreadPool* pool_;
  Task* next_ = nullptr;
  friend class ThreadPool;
};

template <class F, class... Args>
class ThreadPool::TaskImpl final : public Task {
  static auto DoRun(F&& f, // NOLINT(*-rvalue-reference-param-not-moved) -
                           // capture then perfect-forward
    std::tuple<Args...>&& arg_tuple, CancelToken tok) -> decltype(auto)
  {
    return std::apply(
      [&f, tok](Args&&... args) {
        if constexpr (std::is_invocable_v<F, Args..., CancelToken>) {
          return std::forward<F>(f)(std::forward<Args>(args)..., tok);
        } else {
          (void)tok; // no [[maybe_unused]] in lambda captures
          return std::forward<F>(f)(std::forward<Args>(args)...);
        }
      },
      std::move(arg_tuple));
  }
  using Ret = decltype(DoRun(std::declval<F>(),
    std::declval<std::tuple<Args...>>(), std::declval<CancelToken>()));

public:
  TaskImpl(ThreadPool* pool, F f, Args... args)
    : Task(pool)
    , f_(std::forward<F>(f))
    , args_(std::forward<Args>(args)...)
  {
  }

  // ReSharper disable once CppMemberFunctionMayBeStatic
  auto await_ready() const noexcept -> bool { return false; }
  auto await_suspend(detail::Handle h) -> void
  {
    parent_ = h;
    if (!pool_->PushToSubmitQueue(this)) {
      // No space in the submission queue; stash to the local queue
      // to submit later.
      pool_->PushToBackoffSubmitQueue(this);
    }
  }
  auto await_cancel(detail::Handle) noexcept -> bool
  {
    cancelState_.store(Requested, std::memory_order_release);
    return false;
  }
  auto await_must_resume() const noexcept -> bool
  {
    return cancelState_.load(std::memory_order_acquire) != Confirmed;
  }
  auto await_resume() && -> Ret { return std::move(result_).Value(); }

  auto Run() -> void override
  {
    CancelToken token;
    token.state_ = &cancelState_;

    try {
      if constexpr (std::is_same_v<Ret, void>) {
        DoRun(std::forward<F>(f_), std::move(args_), std::move(token));
        result_.StoreValue(detail::Void {});
      } else {
        result_.StoreValue(
          DoRun(std::forward<F>(f_), std::move(args_), std::move(token)));
      }
    } catch (...) {
      result_.StoreException();
    }
  }

private:
  F f_;
  std::tuple<Args...> args_;
  detail::Result<Ret> result_;
  std::atomic<CancelState> cancelState_;
};

template <class F, class... Args>
  requires(std::invocable<F, Args...>
    || std::invocable<F, Args..., ThreadPool::CancelToken>)
auto ThreadPool::Run(F&& f, Args&&... args) -> Awaitable auto
{
  return MakeAwaitable<TaskImpl<F, Args...>, ThreadPool*, F, Args...>(
    this, std::forward<F>(f), std::forward<Args>(args)...);
}

// Type-erased ThreadNotification

namespace detail {
  template <class T, class EventLoopT>
  concept ValidThreadNotification
    = requires(EventLoopT& el, T& n, void (*fn)(void*), void* arg) {
        { T(el, fn, arg) };
        { n.Post(el, fn, arg) } noexcept -> std::same_as<void>;
      };
} // namespace detail

struct ThreadPool::IThreadNotification {
  IThreadNotification() = default;
  virtual ~IThreadNotification() = default;

  OXYGEN_MAKE_NON_COPYABLE(IThreadNotification)
  OXYGEN_DEFAULT_MOVABLE(IThreadNotification)

  virtual auto Post(void (*fn)(void*), void* arg) noexcept -> void = 0;
};

template <class EventLoopT>
struct ThreadPool::ThreadNotificationImpl final : IThreadNotification {
  ThreadNotificationImpl(EventLoopT& el, void (*fn)(void*), void* arg)
    : event_loop(el)
    , impl(el, fn, arg)
  {
    // Validate against the above concept to produce a meaningful
    // compile error upon any mismatches
    ValidateThreadNotification(impl);
  }

  auto Post(void (*fn)(void*), void* arg) noexcept -> void override
  {
    impl.Post(event_loop, fn, arg);
  }

private:
  static auto ValidateThreadNotification(
    detail::ValidThreadNotification<EventLoopT> auto&) -> void
  {
  }

  EventLoopT& event_loop; // NOLINT(*-avoid-const-or-ref-data-members)
  [[no_unique_address]] ThreadNotification<EventLoopT> impl;
};

// Public API

template <class EventLoopT>
ThreadPool::ThreadPool(EventLoopT& el, const unsigned thread_count)
  : d_(new Data)
{
  // Round up the capacity to be a power of 2, so it'll be mutually prime
  // with Stride (see below).
  // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
  d_->sq_capacity = std::bit_ceil(thread_count) * 512;
  d_->sq.reset(new Slot[d_->sq_capacity]);
  d_->notification = std::make_unique<ThreadNotificationImpl<EventLoopT>>(
    el, &ThreadPool::Tick, this);

  d_->threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; ++i) {
    d_->threads.emplace_back([this] { ThreadBody(); });
  }
}

inline ThreadPool::~ThreadPool()
{
  DCHECK_EQ_F(d_->backoff_sq_head, nullptr);

  for (size_t i = 0; i < d_->threads.size(); ++i) {
    const bool ret [[maybe_unused]]
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    = PushToSubmitQueue(reinterpret_cast<Task*>(kExitRequest));
    DCHECK_F(ret);
  }
  for (auto& t : d_->threads) {
    t.join();
  }

  DCHECK_F(d_->sq_head == d_->sq_tail);
  DCHECK_F(d_->cq_head == nullptr);
}

inline auto ThreadPool::ThreadBody() -> void
{
  while (true) {
    Task* t = PopFromSubmitQueue();
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (t == reinterpret_cast<Task*>(kExitRequest)) {
      break;
    }
    t->Run();
    PushToCompletionQueue(t);
  }
}

/*static*/ inline auto ThreadPool::Tick(void* arg) -> void
{
  const auto self = static_cast<ThreadPool*>(arg);

  // Grab a reference to member variables (see below)
  const detail::IntrusivePtr<Data> d = self->d_;

  self->DrainCompletionQueue(); // Note: this may destroy the ThreadPool

  if (d->backoff_sq_head) {
    // If we got here, the ThreadPool must not have been deleted yet
    // (since the non-empty backoffSubmitQueue implies that some task is still
    // blocked in run()).
    // Hopefully worker thread consumed a few tasks from
    // the submission queue, so submit more tasks from the backoff
    // queue if possible.
    self->SubmitBackoffSubmitQueue();
  }
}

// SUBMISSION QUEUE
// ----------------
// This is essentially a shamelessly borrowed folly::MPMCQueue,
// dramatically simplified for the ThreadPool's needs (SPMC, fixed-capacity,
// only non-blocking writes and blocking reads).
//
// The queue is implemented as a circular buffer of single-element SPSC queues,
// ("slots"), with ever-increasing head and tail indices.
//
// Each slot can be in one of three states:
//    - empty (task == nullptr, dequeuing == false);
//    - empty with a blocked reader (task == nullptr, dequeuing == true,
//                                   task.wait() has been called);
//    - inhabited (task != nullptr, dequeuing == false).
//
// popFromSubmitQueue() advances the head, and tries to dequeue the task from
// the slot, suspending on Slot::task if necessary. Note that permits the queue
// head to go beyond the tail, and does not allow SubmitQueue size to be smaller
// than the number of threads.
//
// Stride (hardcoded to 7) is used to prevent false sharing between
// worker threads dequeuing from adjacent slots. Stride needs to be mutually
// prime with the queue capacity (to make sure all slots are used),
// so the capacity is rounded up to a power of 2.

// ReSharper disable once CppMemberFunctionMayBeConst
inline auto ThreadPool::PushToSubmitQueue(Task* task) -> bool
{
  // ReSharper disable once CppUseStructuredBinding
  Slot& slot = d_->sq[d_->sq_tail % d_->sq_capacity];

  Task* prev = nullptr;
  if (!slot.task.compare_exchange_strong(
        prev, task, std::memory_order_acq_rel, std::memory_order_relaxed)) {
    return false;
  }
  if (slot.dequeuing.load(std::memory_order_acquire)) {
    slot.task.notify_one();
  }
  d_->sq_tail += kStride;
  return true;
}

// ReSharper disable once CppMemberFunctionMayBeConst
inline auto ThreadPool::PopFromSubmitQueue() -> Task*
{
  const size_t head = d_->sq_head.fetch_add(kStride, std::memory_order_relaxed);
  Slot& slot = d_->sq[head % d_->sq_capacity];
  while (true) {
    Task* ret = slot.task.exchange(nullptr, std::memory_order_acquire);
    if (ret) {
      return ret;
    }

    // No task available yet; do some spin-waiting to save on syscalls.
    // In ~1% cases, spin longer and adjust the adaptive cutoff.
    const bool update_cutoff = (head % 128 == 0);
    const size_t cutoff = update_cutoff
      ? 20000
      : d_->sq_spin_cutoff.load(std::memory_order_relaxed);
    size_t spins = 0;
    while (
      (ret = slot.task.exchange(nullptr, std::memory_order_acquire)) == nullptr
      && ++spins < cutoff) {
      detail::SpinLoopBody();
    }
    if (ret && update_cutoff) {
      d_->sq_spin_cutoff.store(
        std::max<uint32_t>(
          200u, static_cast<uint32_t>(static_cast<double>(spins) * 1.25)),
        std::memory_order_relaxed);
    }

    if (ret) {
      return ret;
    }

    // Still no task available; suspend the thread.
    slot.dequeuing.store(true, std::memory_order_release);
    slot.task.wait(nullptr, std::memory_order_acquire);
    slot.dequeuing.store(false, std::memory_order_release);
  }
}

// BACKOFF SUBMISSION QUEUE
// ------------------------
// As noted above, the submission queue has fixed capacity, so
// pushToSubmitQueue() may fail. In that case, the task is pushed to the backoff
// queue (implemented as a singly-linked list and therefore having unbounded
// capacity), which is drained later from tick(), after the worker threads
// dequeue some elements from the submission queue (and process them).
//
// The backoff queue is only accessed by the main thread, so no fancy
// synchronization is needed.

// ReSharper disable once CppMemberFunctionMayBeConst
inline auto ThreadPool::PushToBackoffSubmitQueue(Task* task) -> void
{
  task->next_ = nullptr;
  *d_->backoff_sq_tail_ptr = task;
  d_->backoff_sq_tail_ptr = &task->next_;
}

inline auto ThreadPool::SubmitBackoffSubmitQueue() -> void
{
  for (Task* t = d_->backoff_sq_head; t;) {
    Task* next = t->next_;
    if (!PushToSubmitQueue(t)) { // Submission queue at capacity
      d_->backoff_sq_head = t;
      return;
    }
    // Note: the task may have been already dequeued by a worker thread,
    // processed, and pushed to CQ, and its `next` pointer may have been
    // reused; so grab it before calling pushToSubmitQueue().
    t = next;
  }

  // The backoff queue fully submitted
  d_->backoff_sq_head = nullptr;
  d_->backoff_sq_tail_ptr = &d_->backoff_sq_head;
}

// COMPLETION QUEUE
// ----------------
// The completion queue is naturally MPSC, so we can use a textbook
// implementation based on a lock-free stack.
//
// The consumer thread does not dequeue individual elements, but rather
// grabs the entire queue at once, and then processes elements
// at convenient pace.
//
// The first thread to push to the queue is responsible for notifying
// the consumer thread (by posting a tick() event).

inline auto ThreadPool::PushToCompletionQueue(Task* task) -> void
{
  Task* head;
  while (true) {
    head = d_->cq_head.load();
    task->next_ = head;
    if (d_->cq_head.compare_exchange_weak(
          head, task, std::memory_order_release, std::memory_order_relaxed)) {
      break;
    }
  }
  if (!head) {
    d_->notification->Post(&ThreadPool::Tick, this);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
inline auto ThreadPool::DrainCompletionQueue() -> void
{
  const detail::IntrusivePtr<Data> dd = d_;

  while (true) {
    Task* head = dd->cq_head.exchange(nullptr, std::memory_order_acquire);
    if (!head) {
      break;
    }

    // Reverse the list to process completion events in FIFO order
    Task* prev = nullptr;
    Task* curr = head;
    while (curr) {
      Task* next = curr->next_;
      curr->next_ = prev;
      prev = curr;
      curr = next;
    }

    // Process the list
    for (const Task* t = prev; t;) {
      Task* next = t->next_;
      t->parent_.resume();
      // Note: this may have deleted this, so we grabbed
      // `dd` above to extend lifetime of all member
      // variables.
      t = next;
    }
  }
}

} // namespace oxygen::co
