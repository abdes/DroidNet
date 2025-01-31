//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"

// ReSharper disable CppMemberFunctionMayBeStatic

#if !defined(OXCO_AWAITABLE_STATE_DEBUG)
namespace oxygen::co::detail {

//! A runtime validator for the awaitable state machine.
/*!
 This version should compile to nothing and be empty. The version in the other
 branch, when `OXCO_AWAITABLE_STATE_DEBUG` is defined, has the actual checking
 logic.

 These are all noexcept so that any exceptions thrown immediately crash the
 program.
 */
struct AwaitableStateChecker {
    // Mark the end of using this checker to process a particular awaitable.
    // Not necessary if it only handles one awaitable during its lifetime.
    void Reset() noexcept { }

    // Like reset(), but don't check that the awaitable is in a valid state
    // to abandon.
    void ForceReset() { }

    // Note that await_ready() returned the given value. Returns the
    // same value for convenience.
    [[nodiscard]] auto ReadyReturned(auto val) const noexcept { return val; }

    // Note that await_early_cancel() returned the given value. Returns the
    // same value for convenience.
    [[nodiscard]] auto EarlyCancelReturned(auto val) noexcept { return val; }

    // Note that await_set_executor() is about to be invoked.
    void AboutToSetExecutor() noexcept { }

    // Transform a coroutine handle before passing it to await_suspend().
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[nodiscard]] auto AboutToSuspend(const Handle h) noexcept { return h; }

    // Note that await_suspend() threw an exception.
    void SuspendThrew() noexcept { }

    // Transform a coroutine handle before passing it to await_cancel().
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[nodiscard]] auto AboutToCancel(const Handle h) noexcept { return h; }

    // Note that await_cancel() returned the given value. Returns the
    // same value for convenience.
    [[nodiscard]] auto CancelReturned(auto val) noexcept { return val; }

    // Note that await_must_resume() returned the given value. Returns the
    // same value for convenience.
    [[nodiscard]] auto MustResumeReturned(auto val) const noexcept { return val; }

    // Note that await_resume() is about to be invoked.
    void AboutToResume() noexcept { }
};
} // namespace oxygen::co::detail

#else // defined(OXCO_AWAITABLE_STATE_DEBUG)
#  include "Oxygen/Base/Logging.h"
#  include "Oxygen/Base/Unreachable.h"
#  include "Oxygen/OxCo/Detail/ProxyFrame.h"

namespace oxygen::co::detail {

struct AwaitableStateChecker : ProxyFrame {
    // See doc/02_adapting.md for much more detail on this state machine.
    enum class State : uint8_t {
        kInitial, // We haven't done anything with the awaitable yet
        kNotReady, // We called await_ready() and it returned false
        kInitialCxlPend, // Initial + await_early_cancel() returned false
        kCancelPending, // NotReady + await_early_cancel() returned false
        kReadyImmediately, // await_ready() returned true before await_suspend()
        kRunning, // await_suspend() has started
        kCancelling, // Running + await_cancel() returned false
        kReadyAfterCancel, // Resumed from Cancelling or ready after CxlPend;
                           // needs await_must_resume()
        kReady, // Resumed from Running; needs await_resume()
        kCancelled, // Operation complete, without result due to cancel
        kDone, // Operation complete with result (value or error)
    };
    Handle real_handle;
    bool has_executor = false;
    mutable State state = State::kInitial;

    AwaitableStateChecker()
    {
        this->resume_fn = +[](CoroutineFrame* frame) {
            const auto* self = static_cast<AwaitableStateChecker*>(frame); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
            switch (self->state) {
            case State::kRunning:
                self->state = State::kReady;
                break;
            case State::kCancelling:
                self->state = State::kReadyAfterCancel;
                break;

            case State::kInitial:
            case State::kNotReady:
            case State::kInitialCxlPend:
            case State::kCancelPending:
            case State::kReadyImmediately:
            case State::kReadyAfterCancel:
            case State::kReady:
            case State::kCancelled:
            case State::kDone:
                Unreachable();
            }
            self->real_handle.resume();
        };
    }
    ~AwaitableStateChecker() { Reset(); }

    OXYGEN_DEFAULT_COPYABLE(AwaitableStateChecker)
    OXYGEN_DEFAULT_MOVABLE(AwaitableStateChecker)

    void Reset() noexcept
    {
        // If you find that this assertion is firing in State::Ready or
        // State::ReadyImmediately, check whether you're writing a co_await
        // expression inside a CATCH_CHECK() macro or unevaluated portion
        // of a short-circuiting boolean expression. If you are, try not; it
        // runs into a terrible gcc bug which half-evaluates the unevaluated:
        // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=112360
        DCHECK_F(state == State::kCancelled || state == State::kDone || state == State::kInitial);
        ForceReset();
    }

    void ForceReset()
    {
        state = State::kInitial;
        has_executor = false;
    }

    [[nodiscard]] auto ReadyReturned(auto val) const noexcept
    {
        switch (state) {
        case State::kInitial:
        case State::kNotReady:
            state = val ? State::kReadyImmediately : State::kNotReady;
            break;
        case State::kInitialCxlPend:
        case State::kCancelPending:
            state = val ? State::kReadyAfterCancel : State::kCancelPending;
            break;
        case State::kReadyImmediately:
        case State::kReadyAfterCancel:
            // Redundant readiness check is allowed as long as  we don't
            // backtrack in readiness
            DCHECK_F(val);
            break;

        case State::kRunning:
        case State::kCancelling:
        case State::kReady:
        case State::kCancelled:
        case State::kDone:
            Unreachable();
        }
        return val;
    }
    [[nodiscard]] auto EarlyCancelReturned(auto val) noexcept
    {
        switch (state) {
        case State::kInitial:
            state = val ? State::kCancelled : State::kInitialCxlPend;
            break;
        case State::kNotReady:
            state = val ? State::kCancelled : State::kCancelPending;
            break;
        case State::kReadyImmediately:
            state = val ? State::kCancelled : State::kReadyAfterCancel;
            break;

        case State::kInitialCxlPend:
        case State::kCancelPending:
        case State::kRunning:
        case State::kCancelling:
        case State::kReadyAfterCancel:
        case State::kReady:
        case State::kCancelled:
        case State::kDone:
            Unreachable();
        }
        return val;
    }
    void AboutToSetExecutor() noexcept
    {
        DCHECK_F(
            state == State::kNotReady || state == State::kCancelPending || state == State::kReadyImmediately || state == State::kInitial || state == State::kInitialCxlPend);
        has_executor = true;
    }
    [[nodiscard]] auto AboutToSuspend(const Handle h) noexcept
    {
        DCHECK_F(has_executor);
        switch (state) {
        case State::kNotReady:
            state = State::kRunning;
            break;
        case State::kCancelPending:
            state = State::kCancelling;
            break;

        case State::kInitial:
        case State::kInitialCxlPend:
        case State::kReadyImmediately:
        case State::kRunning:
        case State::kCancelling:
        case State::kReadyAfterCancel:
        case State::kReady:
        case State::kCancelled:
        case State::kDone:
            Unreachable();
        }
        real_handle = h;
        this->LinkTo(h);
        return this->ToHandle();
    }
    void SuspendThrew() const noexcept
    {
        DCHECK_F(state == State::kRunning || state == State::kCancelling);
        state = State::kDone;
    }

    [[nodiscard]] auto AboutToCancel([[maybe_unused]] const Handle h) noexcept
    {
        DCHECK_F(state == State::kRunning);
        DCHECK_EQ_F(real_handle, h);
        state = State::kCancelling;
        return this->ToHandle();
    }
    [[nodiscard]] auto CancelReturned(auto val) noexcept
    {
        if (val) {
            DCHECK_EQ_F(state, State::kCancelling);
            state = State::kCancelled;
        }
        return val;
    }
    [[nodiscard]] auto MustResumeReturned(auto val) const noexcept
    {
        DCHECK_EQ_F(state, State::kReadyAfterCancel);
        state = val ? State::kReady : State::kCancelled;
        return val;
    }
    void AboutToResume() const noexcept
    {
        DCHECK_F(state == State::kReadyImmediately || state == State::kReady);
        state = State::kDone;
    }
};
} // namespace oxygen::co::detail

#endif // defined(OXCO_AWAITABLE_STATE_DEBUG)
