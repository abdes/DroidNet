//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Concepts/Awaitable.h"
#include "Detail/ParkingLotImpl.h"

namespace oxygen::co {

//! An event, supporting multiple waiters, which get woken up when the event is
//! triggered.
/*!
 An event is a synchronization primitive that tracks whether something has
 "happened yet", and allows others to wait for the thing to happen. An event can
 only occur once, and once it happens, it cannot be reset.

 Alternatively, the `ParkingLot` can be used to implement waiting for state
 transitions or things that happen again and again.

 Upon construction, the event has not happened (the boolean flag is `false`);
 calling the `Trigger()` method indicates that the event has happened and wakes
 up any tasks that were waiting on it. Waiting for an event that already
 happened is a no-op and does not result in suspension.

 The `Event` itself is an `Awaitable`, which makes it easy for a class to expose
 something that can be used both to check if the event happened and to await for
 it to happen if not:

 \code{.cpp}
    class MyClass {
        oxygen::co::Event connected_;
    public:
        void DoSomething() { connected_.Trigger(); }

        auto& Connected() { return connected_; }
        bool Connected() { return connected_.Triggered(); }
    };
    ...
    // Check if connected and do something if it is.
    if (my.Connected()) { // Do something }

    // Wait for connected and do something when it happens.
    // Will not suspend if it is already connected.
    co_await my.Connected();
 \endcode

 \see ParkingLot
 */
class Event final : public detail::ParkingLotImpl<Event> {
public:
    Event() = default;

    //! Trigger the event, waking any tasks that are waiting for it to occur.
    void Trigger()
    {
        triggered_ = true;
        UnParkAll();
    }

    //! Returns true if the event has happened yet.
    [[nodiscard]] auto Triggered() const noexcept { return triggered_; }

    class Awaitable final : public Parked {
    public:
        using Parked::Parked;
        // ReSharper disable CppMemberFunctionMayBeStatic
        explicit operator bool() { return this->Object().Triggered(); }
        [[nodiscard]] auto await_ready() const noexcept { return this->Object().Triggered(); }
        void await_suspend(const detail::Handle h) { this->DoSuspend(h); }
        void await_resume() { }
        // ReSharper restore CppMemberFunctionMayBeStatic
    };

    //! Returns an awaitable which becomes ready when the event is triggered
    //! and is immediately ready if that has already happened.
    auto operator co_await()
    {
        return Awaitable(*this);
    }

private:
    bool triggered_ = false;
};

static_assert(Awaitable<Event>);
static_assert(Awaitable<Event::Awaitable>);

} // namespace oxygen::co
