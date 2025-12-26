//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/RepeatableShared.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/api_export.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen {

class Platform;

namespace platform {

  class AsyncOps final : public Component, public co::LiveObject {
    OXYGEN_COMPONENT(AsyncOps)
  public:
    AsyncOps(const PlatformConfig& config);
    ~AsyncOps() override;

    //! A utility function, returning an awaitable suspending the caller for a
    //! specified duration. Suitable for use with `AnyOf()` etc.
    auto SleepFor(std::chrono::microseconds delay)
    {
      return co::SleepFor(io_, delay);
    }

    auto HasThreads() const { return threads_ != nullptr; }
    auto Threads() -> co::ThreadPool&
    {
      CHECK_NOTNULL_F(threads_, "Platform not configured with a thread pool");
      return *threads_;
    }

    OXGN_PLAT_API auto PollOne() -> size_t;

    auto Stop() -> void override;

    [[nodiscard]] auto ActivateAsync(co::TaskStarted<> started = {})
      -> co::Co<> override;

    [[nodiscard]] auto IsRunning() const -> bool override
    {
      return nursery_ != nullptr;
    }

    [[nodiscard]] auto Nursery() const -> co::Nursery&
    {
      DCHECK_NOTNULL_F(nursery_);
      return *nursery_;
    }

    [[nodiscard]] auto OnTerminate() -> co::Event& { return terminate_; }

  private:
    auto HandleSignal(const std::error_code& error, int signal_number) -> void;

    asio::io_context io_;
    asio::signal_set signals_;
    std::unique_ptr<co::ThreadPool> threads_;
    co::Event terminate_;
    co::Nursery* nursery_ { nullptr };
  };

  class EventPump final : public Component {
    OXYGEN_COMPONENT(EventPump)
    OXYGEN_COMPONENT_REQUIRES(AsyncOps)
  public:
    EventPump();

    //! Called as part of the main loop to check for pending platform
    //! events, and if any are found, to remove and asynchronously process
    //! __only__ the next one.
    /*!
     \note This method is not asynchronous and needs to complete quickly.
           When at least one event is ready, it resumes `PumpEvent()`,
           which will do the actual processing asynchronously. This
           machinery is internal to the platform. Externally, interested
           parties should await the awaitable appropriate for the event type
           they are interested in.
    */
    OXGN_PLAT_API auto PollOne() -> bool;

    //! Suspends the caller until a platform event is available.
    /*!
     When an event is ready, all suspended tasks are resumed and will have a
     chance to receive it. The next event will not be pumped as long as any
     of the tasks is still processing the current one. That is indicated by
     the task acquiring the lock on the event source via `Lock()` right
     after being resumed, and releasing it when it is done processing the
     event.

     This locking rule ensures that all tasks awaiting the event will have a
     chance to process it before the next one is started, and that tasks are
     scheduled in sequence, each one after the one before it fully
     completes. This is useful for event filtering, event augmentation, and
     for orchestrated processing of events.

     Additionally, all tasks share the same copy of the event. Therefore, an
     earlier task may mark the event as handled to instruct later tasks to
     skip it.
    */
    auto NextEvent() { return event_source_.Next(); }

    //! Acquires exclusive access to the event source, preventing other
    //! tasks from starting and pausing the event pump.
    /*!
     \return An awaitable semaphore lock guard (acquires the semaphore on
     construction, and releases it on destruction).

     \note It is important that the guard is assigned to a variable,
           otherwise it will be returned as a temporary and the lock will be
           released immediately.
    */
    auto Lock() { return event_source_.Lock(); }

    //! Shuts down the event pump, causing all future NextEvent() calls to
    //! complete immediately rather than suspending.
    /*!
     This method should be called during shutdown to prevent coroutines from
     waiting indefinitely on events that will never come. Once shut down,
     the EventPump cannot be restarted.
    */
    auto Shutdown() -> void;

    //! Checks if the EventPump is currently running and processing events.
    /*!
     \return true if the EventPump is running, false if it has been shut down.

     Coroutines processing events should check this in their loop conditions
     to gracefully exit when the EventPump is no longer active.
    */
    [[nodiscard]] auto IsRunning() const -> bool
    {
      return !shutdown_requested_.load();
    }

  private:
    co::RepeatableShared<PlatformEvent> event_source_;
    co::ParkingLot poll_;
    std::atomic<bool> shutdown_requested_ { false };
  };

  class InputEvents final : public Component {
    OXYGEN_COMPONENT(InputEvents)
    OXYGEN_COMPONENT_REQUIRES(AsyncOps, EventPump)
  public:
    //! The maximum number of events buffered in the input events channel before
    //! it starts blocking on writes.
    static constexpr size_t kMaxBufferedEvents = 32;

    auto ForRead() { return channel_.ForRead(); }
    auto ForWrite() -> co::BroadcastChannel<InputEvent>::Writer&
    {
      return channel_writer_;
    }

  protected:
    auto UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) noexcept
      -> void override
    {
      // NOLINTBEGIN(*-pro-type-static-cast-downcast)
      // Must always be present even in headless mode
      async_ = observer_ptr { &static_cast<AsyncOps&>(
        get_component(AsyncOps::ClassTypeId())) };
      // Optional if we are in headless mode, so let's deal with it not being
      // there and an exception thrown by the get_component call.
      try {
        event_pump_ = observer_ptr { &static_cast<EventPump&>(
          get_component(EventPump::ClassTypeId())) };
      } catch (const ComponentError&) {
        // If it's not there, we will simply not pull events
        (void)0;
      }
      // NOLINTEND(*-pro-type-static-cast-downcast)
    }

  private:
    friend Platform;
    [[nodiscard]] auto ProcessPlatformEvents() -> co::Co<>;
    [[nodiscard]] auto IsRunning() const
    {
      return event_pump_ != nullptr && event_pump_->IsRunning();
    };

    co::BroadcastChannel<InputEvent> channel_ { kMaxBufferedEvents };
    // NOLINTNEXTLINE(*-avoid-const-or-ref-data-members) - lifetime is linked to
    // channel_
    co::BroadcastChannel<InputEvent>::Writer& channel_writer_
      = channel_.ForWrite();

    observer_ptr<AsyncOps> async_ { nullptr };
    observer_ptr<EventPump> event_pump_ { nullptr };
  };

  class WindowManager final : public Component {
    OXYGEN_COMPONENT(WindowManager)
    OXYGEN_COMPONENT_REQUIRES(AsyncOps, EventPump)
  public:
    OXGN_PLAT_API auto MakeWindow(const window::Properties& props)
      -> std::weak_ptr<Window>;

    [[nodiscard]] auto LastWindowClosed() -> co::Event&
    {
      return last_window_closed_;
    }

    //! Queue a window for closing at the next frame start
    OXGN_PLAT_API auto QueueWindowForClosing(WindowIdType window_id) -> void;

    //! Process all windows queued for closing
    OXGN_PLAT_API auto ProcessPendingCloses() -> void;

    //! Return a read-only view of windows queued for closing.
    /*! Used by higher-level callers to perform pre-shutdown notifications
        before the native window teardown occurs. */
    [[nodiscard]] auto GetPendingCloses() const
      -> const std::vector<WindowIdType>&
    {
      return pending_close_windows_;
    }

    //! Scan for windows that are pending close and queue them
    OXGN_PLAT_API auto ScanForPendingCloses() -> void;

  protected:
    auto UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) noexcept
      -> void override
    {
      async_ = &static_cast<AsyncOps&>(get_component(AsyncOps::ClassTypeId()));
      event_pump_
        = &static_cast<EventPump&>(get_component(EventPump::ClassTypeId()));
    }

  private:
    friend Platform;
    [[nodiscard]] auto ProcessPlatformEvents() -> co::Co<>;

    [[nodiscard]] auto IsRunning() const
    {
      return event_pump_ != nullptr && event_pump_->IsRunning();
    };

    [[nodiscard]] auto WindowFromId(WindowIdType window_id) const -> Window&;

    auto ReleaseAllWindows() -> void
    {
      LOG_SCOPE_FUNCTION(1);
      windows_.clear();
    }

    AsyncOps* async_ { nullptr };
    EventPump* event_pump_ { nullptr };
    co::Event last_window_closed_;

    std::vector<std::shared_ptr<Window>> windows_;
    std::vector<WindowIdType> pending_close_windows_;
  };

} // namespace platform

class Platform final : public Composition, public co::LiveObject {
public:
  OXGN_PLAT_API explicit Platform(const PlatformConfig& config);
  OXGN_PLAT_API ~Platform() override;

  OXYGEN_MAKE_NON_COPYABLE(Platform)
  OXYGEN_MAKE_NON_MOVABLE(Platform)

  OXGN_PLAT_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;
  OXGN_PLAT_API auto Run() -> void override;
  OXGN_PLAT_NDAPI auto IsRunning() const -> bool override;
  OXGN_PLAT_API auto Stop() -> void override;

  OXGN_PLAT_API auto Shutdown() -> co::Co<>;

  //! Called at the start of each frame to handle deferred operations
  OXGN_PLAT_API auto OnFrameStart() -> void;

  //! Called at the end of each frame to perform deferred teardown (e.g. native
  //! window destruction queued earlier in the frame).
  OXGN_PLAT_API auto OnFrameEnd() -> void;

  // Pre-destroy hook API: allow parties (e.g. engine modules) to register a
  // callback invoked for each window about to be destroyed in OnFrameEnd().
  using WindowAboutToBeDestroyedHandler
    = std::function<void(platform::WindowIdType)>;

  OXGN_PLAT_API auto RegisterWindowAboutToBeDestroyedHandler(
    WindowAboutToBeDestroyedHandler handler) -> size_t;

  OXGN_PLAT_API void UnregisterWindowAboutToBeDestroyedHandler(size_t token);

  auto Async() -> platform::AsyncOps&
  {
    return GetComponent<platform::AsyncOps>();
  }
  auto Async() const -> const platform::AsyncOps&
  {
    return GetComponent<platform::AsyncOps>();
  }
  auto HasThreads() const { return Async().HasThreads(); }
  auto Threads() -> auto& { return Async().Threads(); }

  auto Events() -> platform::EventPump&
  {
    return GetComponent<platform::EventPump>();
  }
  auto Input() -> platform::InputEvents&
  {
    return GetComponent<platform::InputEvents>();
  }
  auto Windows() -> platform::WindowManager&
  {
    return GetComponent<platform::WindowManager>();
  }

  //! Register a single platform event filter callable executed before
  //! standard platform processors. Only one filter is supported; attempts
  //! to register a second one will abort.
  /*!
    Accept any `Callable` that is invocable with a single argument of type
    `PlatformEvent&` and whose return type is exactly `void`. The callable is
    type-erased and stored as `std::function<void(PlatformEvent&)>`.
  */
  template <typename Callable>
    requires std::invocable<Callable, const platform::PlatformEvent&>
    && std::same_as<void,
      std::invoke_result_t<Callable, const platform::PlatformEvent&>>
  void RegisterEventFilter(Callable&& filter)
  {
    CHECK_F(!event_filter_, "only one platform event filter is supported");
    // Erase the callable into a std::function<void(platform::PlatformEvent&)>
    event_filter_ = std::function<void(const platform::PlatformEvent&)>(
      std::forward<Callable>(filter));
  }

  void ClearEventFilter() { event_filter_ = {}; }

  static OXGN_PLAT_API auto GetInputSlotForKey(platform::Key key)
    -> platform::InputSlot;

  [[nodiscard]] auto GetPhysicalClock() const noexcept
    -> const time::PhysicalClock&
  {
    return physical_clock_;
  }

private:
  std::unordered_map<size_t, WindowAboutToBeDestroyedHandler>
    window_about_to_be_destroyed_handlers_;
  std::atomic<size_t> window_about_to_be_destroyed_next_id_ { 1 };
  auto Compose(const PlatformConfig& config) -> void;

  [[nodiscard]] auto FilterPlatformEvents() -> co::Co<>;
  std::function<void(const platform::PlatformEvent&)> event_filter_;

  time::PhysicalClock physical_clock_;
};

} // namespace oxygen
