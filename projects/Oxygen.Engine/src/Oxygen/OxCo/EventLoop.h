//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::co {

//! A unique identifier for an `EventLoop`, used to tell whether
//! two executors are running in the same environment or whether one
//! is nested in the other.
/*!
 Typically, each event loop runs independently of other, unless explicitly
 nested when `run()` is called again in a synchronous function that starts its
 own event loop.

 In general, you can use the address of the event loop object as its ID unless
 you have a situation where the same underlying event loop can be accessed via
 multiple objects.
*/
class EventLoopID {
    const void* id_;

public:
    explicit constexpr EventLoopID(const void* id)
        : id_(id)
    {
    }

    explicit constexpr operator const void*() const { return id_; }
    [[nodiscard]] auto Get() const noexcept { return id_; }

    constexpr auto operator==(const EventLoopID& other) const -> bool = default;
    constexpr auto operator<=>(const EventLoopID& other) const = default;
};

//! `EventLoop` traits for a type, which need to be specialized to adapt a
//! certain event loop type.
/*!
 Since async functions only run when awaited, we need to call `run()` to
 bootstrap the async portion of the program. Several types of event loops exist
 within `Oxygen`, including:
   - Frame rendering loop,
   - Asynchronous IO event loop,
   - Platform input event loop,
   - ...

 Each of these loops has its own way of running and stopping, and is controlled
 by the engine main loop. By specializing these traits, we can implement a
 uniform way to integrate events and synchronization objects of different types
 into the coroutines framework of `oxygen::co`.

 \b Example:
 \code{cpp}
 class MyEventLoop {
     // Implementation of MyEventLoop
 };

 template <>
 struct EventLoopTraits<MyEventLoop> {
     static auto EventLoopId(MyEventLoop& loop) {
         // Implementation for MyEventLoop
         return EventLoopID(&loop);
     }

     static void Run(MyEventLoop& loop) { ... }
     static void Stop(MyEventLoop& loop) { ... }
     static bool IsRunning(MyEventLoop& loop) noexcept { return true; }
 };
 \endcode
 */
template <class T, class = void>
struct EventLoopTraits {
    // Enforce that the traits are specialized for a specific event loop type.
    // Prevents instantiation of the generic version.
    static_assert(!std::is_same_v<T, T>,
        "you must specialize EventLoopTraits for a specific event loop class");

    // ReSharper disable CppFunctionIsNotImplemented

    //! Returns a unique identifier for the event loop. In general, you can use
    //! the address of the event loop object as its ID.
    static auto EventLoopId(T&) -> EventLoopID;

    //! Runs the event loop.
    static void Run(T&);

    //! Tells the event loop to stop, which will cause `Run()` to return shortly
    //! thereafter.
    static void Stop(T&);

    //! Tests whether we're inside this event loop.
    /*! Only used for preventing nested runs using the same event loop;
     if no suitable implementation is available, may always return false,
     or leave undefined for the same effect.
    */
    static auto IsRunning(T&) noexcept -> bool;

    // ReSharper restore CppFunctionIsNotImplemented
};

} // namespace oxygen::co
