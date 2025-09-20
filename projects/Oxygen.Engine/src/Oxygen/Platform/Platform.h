//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <asio/signal_set.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/RepeatableShared.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Platform/Display.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Platform/api_export.h>

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
    auto ForRead() { return channel_.ForRead(); }

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

    co::BroadcastChannel<InputEvent> channel_ { 8 };
    // NOLINTNEXTLINE(*-avoid-const-or-ref-data-members) - lifetime is linked to
    // channel_
    co::BroadcastChannel<InputEvent>::Writer& channel_writer_
      = channel_.ForWrite();

    AsyncOps* async_ { nullptr };
    EventPump* event_pump_ { nullptr };
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

  static OXGN_PLAT_API auto GetInputSlotForKey(platform::Key key)
    -> platform::InputSlot;

  [[nodiscard]] auto GetPhysicalClock() const noexcept
    -> const time::PhysicalClock&
  {
    return physical_clock_;
  }

private:
  auto Compose(const PlatformConfig& config) -> void;

  time::PhysicalClock physical_clock_ {};
};

#if 0
namespace imgui {
    class ImGuiPlatformBackend;
} // namespace imgui

class PlatformBase {
public:
    OXGN_PLAT_API PlatformBase();
    virtual ~PlatformBase() = default;

    OXYGEN_MAKE_NON_COPYABLE(PlatformBase)
    OXYGEN_MAKE_NON_MOVABLE(PlatformBase)

    // ---------------------------------------------------------------------------

#  if defined(OXYGEN_VULKAN)
    [[nodiscard]] virtual auto GetRequiredInstanceExtensions() const
        -> std::vector<const char*>
        = 0;
#  endif // OXYGEN_VULKAN

    // -- Displays ---------------------------------------------------------------

    [[nodiscard]] virtual auto Displays() const
        -> std::vector<std::unique_ptr<platform::Display>>
        = 0;

    [[nodiscard]] virtual auto DisplayFromId(
        const platform::Display::IdType& display_id) const
        -> std::unique_ptr<platform::Display>
        = 0;

    // -- Window Management ------------------------------------------------------

    virtual auto MakeWindow(std::string const& title, PixelExtent const& extent)
        -> std::weak_ptr<platform::Window>
        = 0;

    virtual auto MakeWindow(std::string const& title,
        PixelExtent const& extent,
        platform::Window::InitialFlags flags)
        -> std::weak_ptr<platform::Window>
        = 0;

    virtual auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent)
        -> std::weak_ptr<platform::Window>
        = 0;

    virtual auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent,
        platform::Window::InitialFlags flags)
        -> std::weak_ptr<platform::Window>
        = 0;

    // -- Events -----------------------------------------------------------------

    //! Poll the platform event loop to process one event, awaking and executing
    //! all async handlers waiting for that event.
    /*!
     The `PollOne()` method polls for at most one platform event, and executes
     all async handlers that are ready to run, without blocking.

     \return The number of async event handlers that were executed.
     */
    virtual auto PollOne() -> size_t = 0;

    auto InputEvent() -> co::ParkingLot& { return platform_input_; }
    auto LastWindowClosed() -> co::Event& { return last_window_closed_; }

    // -- Slots ------------------------------------------------------------------

    [[nodiscard]] auto OnLastWindowClosed() -> auto&
    {
        return on_last_window_closed_;
    }

    [[nodiscard]] auto OnWindowClosed() -> auto& { return on_window_closed_; }

    [[nodiscard]] auto OnDisplayConnected() -> auto&
    {
        return on_display_connected_;
    }

    [[nodiscard]] auto OnDisplayDisconnected() -> auto&
    {
        return on_display_disconnected_;
    }

    // To get the new orientation, find the display from its id and query its
    // orientation.
    [[nodiscard]] auto OnDisplayOrientationChanged() -> auto&
    {
        return on_display_orientation_changed_;
    }

    static void GetAllInputSlots(std::vector<platform::InputSlot>& out_keys);
    static OXGN_PLAT_API auto GetInputSlotForKey(platform::Key key) -> platform::InputSlot;

    auto GetInputCategoryDisplayName(std::string_view category_name) -> std::string_view;

    [[nodiscard]] virtual auto CreateImGuiBackend(platform::WindowIdType window_id) const
        -> std::unique_ptr<imgui::ImGuiPlatformBackend>
        = 0;

private:
    co::ParkingLot platform_input_;
    co::Event last_window_closed_;

    sigslot::signal<> on_last_window_closed_;
    sigslot::signal<platform::Window const&> on_window_closed_;

    sigslot::signal<platform::Display::IdType> on_display_connected_;
    sigslot::signal<platform::Display::IdType> on_display_disconnected_;
    sigslot::signal<platform::Display::IdType> on_display_orientation_changed_;
};

template <typename T>
class PlatformEvent {
public:
    [[nodiscard]] auto NativeEvent() const& { return native_event_; }
    [[nodiscard]] auto NativeEventPtr() { return &native_event_; }
    [[nodiscard]] auto IsHandled() const { return handled_; }
    void SetHandled() { handled_ = true; }

private:
    bool handled_ { false };
    T native_event_ {};
};

template <typename T>
class EventProducer {
public:
    auto Event() const& { return polled_event_; }

protected:
    std::optional<PlatformEvent<T>> polled_event_;
};
#endif

} // namespace oxygen
