//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <tuple>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Semaphore.h>
#include <Oxygen/OxCo/Shared.h>

namespace oxygen::co {

// Define the VoidProducer concept
template <typename F, typename... Args>
concept VoidProducer = requires(F f, Args... args) {
  { std::invoke(f, args...) } -> std::same_as<void>;
};

// Define the ValueProducer concept
template <typename F, typename ValueT, typename... Args>
concept ValueProducer = requires(F f, Args... args) {
  { std::invoke(f, args...) } -> std::same_as<ValueT>;
};

// Define the AsyncVoidProducer concept
template <typename F, typename... Args>
concept AsyncVoidProducer = requires(F f, Args... args) {
  { std::invoke(f, args...) } -> std::same_as<Co<void>>;
};

// Define the AsyncValueProducer concept
template <typename F, typename ValueT, typename... Args>
concept AsyncValueProducer = requires(F f, Args... args) {
  { std::invoke(f, args...) } -> std::same_as<Co<ValueT>>;
};

// Define the ValidProducer concept
template <typename F, typename ValueT, typename... Args>
concept ValidProducer
  = VoidProducer<F, Args...> || ValueProducer<F, ValueT, Args...>
  || AsyncVoidProducer<F, Args...> || AsyncValueProducer<F, ValueT, Args...>;

//! A wrapper that allows repeated awaiting of a shared asynchronous operation.
/*!
 \tparam ValueT The type of the value produced by the operation.

 RepeatableShared encapsulates a double-buffered shared operation that can be
 awaited multiple times. It ensures sequential access through synchronization
 and prevents new iterations from starting until all awaiters have completed
 processing the current one.

 __Example__:
 \code{.cpp}
    struct WeatherData {
        float temperature;
        float humidity;
        std::chrono::system_clock::time_point timestamp;
    };

    // Define the fetch function type
    using FetchWeather = std::function<Co<WeatherData>()>;

    // Create a fetcher that uses the provided event loop
    auto make_fetcher = [](EventLoop& loop) -> Co<WeatherData> {
        co_await loop.Sleep(100ms);  // Simulate sensor delay
        co_return WeatherData{
            .temperature = 20.0f,
            .humidity = 65.0f,
            .timestamp = loop.Now()
        };
    };

    // Temperature monitoring coroutine
    auto temp_monitor = [](EventLoop& loop, RepeatableShared<FetchWeather>&
 weather) -> Co<void> { for (int i = 0; i < 3; i++) { auto data = co_await
 weather.Next(); auto lock = co_await weather.Lock(); std::cout << "Temperature
 Monitor: " << data.temperature
                     << "C at " << data.timestamp.time_since_epoch().count() <<
 "\n"; co_await loop.Sleep(50ms);  // Simulate processing time
        }
    };

    // Humidity monitoring coroutine
    auto humid_monitor = [](EventLoop& loop, RepeatableShared<FetchWeather>&
 weather) -> Co<void> { for (int i = 0; i < 3; i++) { auto data = co_await
 weather.Next(); auto lock = co_await weather.Lock(); std::cout << "Humidity
 Monitor: " << data.humidity
                     << "% at " << data.timestamp.time_since_epoch().count() <<
 "\n"; co_await loop.Sleep(75ms);  // Different processing time
        }
    };

    int main() {
        EventLoop loop;

        // Create repeatable shared operation using the fetch function
        RepeatableShared<FetchWeather> weather{[&loop]() { return
 make_fetcher(loop); }};

        Run(loop, [&]() -> Co<void> {
            co_await AllOf(
                temp_monitor(loop, weather),
                humid_monitor(loop, weather)
            );
            std::cout << "Monitoring complete\n";
        });

        return 0;
    }
 \endcode
*/
template <typename ValueT> class RepeatableShared {
public:
  // Constructor for callable with no arguments
  template <typename F>
    requires(std::invocable<F> && ValidProducer<F, ValueT>)
  explicit RepeatableShared(F&& func)
    : wrapper_([this, f = std::forward<F>(func)]() -> Co<ValueT> {
      auto guard = co_await CurrentSlot().Lock();
      if constexpr (VoidProducer<F>) {
        std::invoke(f);
        CompleteIteration();
        co_return;
      } else if constexpr (AsyncVoidProducer<F>) {
        co_await std::invoke(f);
        CompleteIteration();
        co_return;
      } else if constexpr (AsyncValueProducer<F, ValueT>) {
        ValueT result = co_await std::invoke(f);
        CompleteIteration();
        co_return result;
      } else if constexpr (ValueProducer<F, ValueT>) {
        ValueT result = std::invoke(f);
        CompleteIteration();
        co_return result;
      }
    })
  {
  }

  // Constructor for callable with arguments
  template <typename F, typename... Args>
    requires(std::invocable<F, Args...> && ValidProducer<F, ValueT, Args...>)
  explicit RepeatableShared(F&& func, Args&&... args)
    : wrapper_([this, f = std::forward<F>(func),
                 args_tuple = std::make_tuple(
                   std::forward<Args>(args)...)]() -> Co<ValueT> {
      auto guard = co_await CurrentSlot().Lock();
      if constexpr (VoidProducer<F, Args...>) {
        std::apply(f, args_tuple);
        CompleteIteration();
        co_return;
      } else if constexpr (AsyncVoidProducer<F, Args...>) {
        co_await std::apply(f, args_tuple);
        CompleteIteration();
        co_return;
      } else if constexpr (AsyncValueProducer<F, ValueT, Args...>) {
        ValueT result = co_await std::apply(f, args_tuple);
        CompleteIteration();
        co_return result;
      } else if constexpr (ValueProducer<F, ValueT, Args...>) {
        ValueT result = std::apply(f, args_tuple);
        CompleteIteration();
        co_return result;
      }
    })
  {
  }

  ~RepeatableShared()
  {
    DLOG_IF_F(WARNING,
      event_slots_ && event_slots_->slot1
        && !(event_slots_->slot1.GetAwaitable().Done()
          || event_slots_->slot1.GetAwaitable().Closed()),
      "RepeatableShared destroyed while not done");
    DLOG_IF_F(WARNING,
      event_slots_ && event_slots_->slot2
        && !(event_slots_->slot1.GetAwaitable().Done()
          || event_slots_->slot1.GetAwaitable().Closed()),
      "RepeatableShared destroyed while not done");
  }

  //! Copy constructor creates a new RepeatableShared that shares state with
  //! the original.
  /*!
   Creates a new RepeatableShared that shares the same underlying slots and
   state with the original. This means:
     - Awaiting on either object or its copies has identical effects.
     - All copies share synchronization through the same semaphore.
  */
  RepeatableShared(const RepeatableShared& other)
    : wrapper_(other.wrapper_)
    , current_slot_index_(other.current_slot_index_)
    , bootstrapped_(other.bootstrapped_)
  {
    if (bootstrapped_) {
      event_slots_ = other.event_slots_;
    }
  }

  //! Copy assignment operator updates this object to share state with the
  //! source.
  /*!
   Makes this RepeatableShared share the same underlying slots and state with
   the source object, similar to copy construction.

   Any previous state of this object is destroyed before adopting the new
   shared state, which requires that the object is in a valid stable state,
   not awaiting a result or pending a cancellation.
  */
  RepeatableShared(RepeatableShared&& other) noexcept
    : wrapper_(std::move(other.wrapper_))
    , current_slot_index_(other.current_slot_index_)
    , bootstrapped_(other.bootstrapped_)
  {
    if (bootstrapped_) {
      event_slots_ = std::move(other.event_slots_);
    }
    other.bootstrapped_ = false;
  }

  //! Move constructor transfers ownership of the shared state.
  /*!
   Transfers ownership of the slots and state from the source object to the
   new object. After the move:
   - The source object is reset to an uninitialized state.
   - The source object can be safely destroyed or reinitialized.
  */
  auto operator=(const RepeatableShared& other) -> RepeatableShared&
  {
    if (this != &other) {
      wrapper_ = other.wrapper_;
      bootstrapped_ = other.bootstrapped_;
      current_slot_index_ = other.current_slot_index_;
      if (bootstrapped_) {
        event_slots_ = other.event_slots_;
      }
    }
    return *this;
  }

  //! Move assignment operator transfers ownership of the shared state
  /*!
   Transfers ownership of the slots and state from the source object to this
   object, similar to move construction.

   Any previous state of this object is destroyed before adopting the new
   shared state, which requires that the object is in a valid stable state,
   not awaiting a result or pending a cancellation.
  */
  auto operator=(RepeatableShared&& other) noexcept -> RepeatableShared&
  {
    if (this != &other) {
      wrapper_ = std::move(other.wrapper_);
      bootstrapped_ = other.bootstrapped_;
      current_slot_index_ = other.current_slot_index_;
      if (bootstrapped_) {
        event_slots_ = std::move(other.event_slots_);
      }
      other.bootstrapped_ = false;
    }
    return *this;
  }

  //! Gets the next iteration of the shared operation.
  /*!
   Returns a reference to the next available Shared<AsyncEventPumper> that can
   be awaited. If this is the first call, it will initialize the internal
   state. The returned reference remains valid until the next iteration
   starts.

   \return Const reference to the next Shared<AsyncEventPumper> ready for
   awaiting.
  */
  auto Next()
  {
    MaybeBootstrap();
    return NextSlot().GetAwaitable();
  }

  //! Acquires exclusive access to the current iteration.
  /*!
   Returns an awaitable lock that ensures sequential access to the current
   iteration. Must be acquired by coroutines awaiting the result of Next(),
   when they are interested in the next result, for as long as the current
   result is being processed. Is acquired automatically by `RepeatableShared`
   before initiating the next iteration.

   In the simplest scenario, the lock will be acquired at the start of the
   coroutine and released at the end. This ensures that all coroutines have a
   chance to process the result of the current iteration before the next one
   starts. It also guarantees that coroutines awaiting the shared operation
   will be scheduled in sequence, each one after the previous one fully
   completes. This is useful for event filtering, event augmentation, and for
   orchestrated processing of events.

   \return An awaitable semaphore lock guard.
  */
  auto Lock() { return CurrentSlot().Lock(); }

private:
  // Add these new types and helper methods before the public section
  using WrappedValueProduce = std::function<Co<ValueT>()>;

  //! Completes the current iteration and prepares the next slot.
  /*!
   Called after a value has been produced and all awaiters have processed
   it. Switches to the other slot and initializes it for the next
   iteration.
  */
  void CompleteIteration()
  {
    current_slot_index_ ^= 1;
    NextSlot().Initialize(this);
  }

  void MaybeBootstrap()
  {
    if (bootstrapped_) {
      return;
    }
    current_slot_index_ = 0;
    event_slots_ = std::make_shared<Slots>();
    NextSlot().Initialize(this);
    bootstrapped_ = true;
  }

  struct Slot {
    Semaphore ready;
    // Store a `Shared` of our wrapper type. Use union for manual lifetime
    // control of shared_awaitable. Unions are not automatically
    // constructed/destroyed by the compiler.
    union {
      Shared<WrappedValueProduce> shared_awaitable;
    };

    // Default constructor - does not initialize shared_awaitable
    // ReSharper disable once CppPossiblyUninitializedMember
    Slot() noexcept
      : ready(1)
    {
    }

    // Destructor - explicitly destroy shared_awaitable if initialized
    ~Slot()
    {
      if (initialized) {
        shared_awaitable.~Shared();
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(Slot)
    OXYGEN_MAKE_NON_MOVABLE(Slot)

    //! Prepares this slot for the next iteration of the repeatable
    //! operation
    /*!
     This method creates a new iteration of the shared operation, ensuring
     proper synchronization and isolation between iterations. It takes the
     awaitable (either stored or constructed) and wraps it in a coroutine
     that manages access control and iteration transitions.

     The wrapping process creates a protective layer around the awaitable:
     - Before the awaitable starts: acquires the slot's semaphore.
     - After the awaitable completes: triggers the transition to next
       iteration.

     Each iteration gets its own wrapped instance, preventing interference
     between different iterations while allowing multiple awaiters to share
     the same iteration's result. When an iteration completes, the wrapper
     automatically prepares the next slot, maintaining the double-buffering
     pattern.

     Contract:
     - Each iteration runs in isolation from the next one.
     - Multiple awaiters can share an iteration's result.
     - Iteration N+1 won't start until iteration N is fully processed.
     - Construction state is preserved between iterations.

     \param parent Pointer to owning RepeatableShared instance that manages
     iteration transitions and construction state
    */
    void Initialize(RepeatableShared* parent)
    {
      if (initialized) {
        shared_awaitable.~Shared();
      }
      new (&shared_awaitable) Shared<WrappedValueProduce>(
        [parent]() -> co::Co<ValueT> { co_return co_await parent->wrapper_; });
      initialized = true;
    }

    auto Lock() -> Semaphore::Awaiter<Semaphore::LockGuard>
    {
      return ready.Lock();
    }

    explicit operator bool() const { return initialized; }

    [[nodiscard]] auto GetAwaitable() const
      -> const Shared<WrappedValueProduce>&
    {
      DCHECK_F(initialized, "Slot not initialized");
      return shared_awaitable;
    }

  private:
    bool initialized { false }; // Explicit initialization tracking
  };

  struct Slots {
    Slot slot1;
    Slot slot2;
  };

  auto CurrentSlot() -> Slot&
  {
    return current_slot_index_ == 0 ? event_slots_->slot1 : event_slots_->slot2;
  }
  auto NextSlot() -> Slot&
  {
    return current_slot_index_ == 0 ? event_slots_->slot2 : event_slots_->slot1;
  }

  std::shared_ptr<Slots> event_slots_ { nullptr };
  WrappedValueProduce wrapper_;
  uint8_t current_slot_index_ { 0 };
  bool bootstrapped_ { false };
};

} // namespace oxygen::co
