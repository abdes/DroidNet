//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Unreachable.h"
#include "Oxygen/OxCo/Detail/Optional.h"
#include "Oxygen/OxCo/Detail/PointerBits.h"
#include "Oxygen/OxCo/Detail/ProxyFrame.h"
#include "Oxygen/OxCo/Detail/Result.h"
#include "Oxygen/OxCo/Detail/SanitizedAwaiter.h"

#include <optional>

namespace oxygen::co::detail {

/// A utility helper accompanying an Awaitable with its own coroutine_handle<>,
/// so AnyOf/AllOf can figure out which awaitable has completed, and takes care
/// of not cancelling things twice.
template <class MuxT, class Aw>
class MuxHelper : public ProxyFrame {
    using Self = MuxHelper;
    using Ret = AwaitableReturnType<Aw>;
    using StorageType = typename Storage<Ret>::Type;

public:
    enum class State : uint8_t {
        kNotStarted = 0, // before await_suspend()
        kCancellationPending = 1, // kNotStarted + await_early_cancel() returned false
        kRunning = 2, // after await_suspend()
        kCancelling = 3, // kRunning + await_cancel() returned false
        kCancelled = 4, // cancellation confirmed
        kReady = 5, // completed but value/exception not yet extracted
        kSucceeded = 6, // awaitable completed and yielded a value
        kFailed = 7, // awaitable completed and yielded an exception
    };

protected:
    static constexpr size_t kStateWidth = 3;

    [[nodiscard]] auto GetState() const noexcept -> State { return mux_.Bits(); }
    [[nodiscard]] auto InState(State state) const noexcept { return GetState() == state; }

    static_assert(static_cast<size_t>(State::kFailed) < 1 << kStateWidth);

public:
    explicit MuxHelper(Aw&& aw) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        : mux_(nullptr, State::kNotStarted)
        , awaitable_(std::forward<Aw>(aw))
    {
    }

    OXYGEN_MAKE_NON_COPYABLE(MuxHelper)
    OXYGEN_MAKE_NON_MOVABLE(MuxHelper)

    ~MuxHelper()
    {
        if (InState(State::kSucceeded)) {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            reinterpret_cast<StorageType*>(storage_)->~StorageType();
        }
    }

    static auto IsSkippable() { return Skippable<AwaiterType<Aw>>; }

    void SetExecutor(Executor* ex) noexcept
    {
        if (InState(State::kNotStarted) || InState(State::kCancellationPending)) {
            awaitable_.await_set_executor(ex);
        }
    }

    [[nodiscard]] auto IsReady() const noexcept
    {
        switch (GetState()) {
        case State::kNotStarted:
            return awaitable_.await_ready();
        case State::kRunning:
        case State::kCancelling:
            Unreachable();
        case State::kCancellationPending:
            // If the parent task has pending cancellation, we want
            // to execute the more involved logic in kickOff().
            [[fallthrough]];
        case State::kCancelled:
            return false;
        case State::kReady:
        case State::kSucceeded:
        case State::kFailed:
            return true;
        }
        Unreachable();
    }

    void Bind(MuxT& mux)
    {
        mux_.Set(&mux, mux_.Bits());
        LinkTo(mux.Parent());
    }

    [[nodiscard]] auto IsBound() const { return Mux() != nullptr; }

    void Suspend()
    {
        switch (GetState()) {
        case State::kNotStarted:
        case State::kCancellationPending:
            KickOff();
            break;
        case State::kCancelled:
            Mux()->Invoke(nullptr);
            break;
        case State::kRunning:
        case State::kCancelling:
        case State::kReady:
        case State::kSucceeded:
        case State::kFailed:
            Unreachable();
        }
    }

    //! Returns true if this awaitable is now cancelled and will not be
    //! completing with a result or exception.
    auto Cancel() -> bool
    {
        switch (GetState()) {
        case State::kNotStarted:
            if (awaitable_.await_early_cancel()) {
                SetState(State::kCancelled);
                if (MuxT* m = Mux()) {
                    m->Invoke(nullptr);
                } else {
                    // If we don't have a mux yet, this is an early cancel
                    // (before Suspend()) and we'll delay the Invoke() call
                    // until either Suspend() or ReportImmediateResult() (at
                    // most one of these two will be called).
                }
                return true;
            }
            SetState(State::kCancellationPending);
            break;
        case State::kRunning:
            SetState(State::kCancelling);
            if (awaitable_.await_cancel(this->ToHandle())) {
                SetState(State::kCancelled);
                Mux()->Invoke(nullptr);
                return true;
            }
            if (InState(State::kCancelled)) {
                // Perhaps await_cancel() did wind up synchronously resuming the
                // handle, even though it returned false.
                return true;
            }
            break;
        case State::kCancelled:
            return true;
        case State::kCancellationPending:
        case State::kCancelling:
        case State::kReady:
        case State::kSucceeded:
        case State::kFailed:
            break;
        }
        return false;
    }

    // Handle the mux await_resume() having been called without await_suspend().
    // The precondition for this is the mux await_ready(): enough awaitables
    // are ready and the rest can be skipped.
    void ReportImmediateResult()
    {
        if (InState(State::kCancellationPending)) {
            // Early-cancel failed then awaitable was ready -> must check
            // await_must_resume. This would have happened in MustResume() if
            // the cancellation came from outside, but happens here otherwise
            // (e.g. AnyOf cancelling remaining awaitables after the first one
            // completes).
            SetState(awaitable_.await_must_resume() ? State::kReady
                                                    : State::kCancelled);
        }
        if (InState(State::kCancelled)) {
            // Already cancelled, just need to notify the mux (we weren't bound
            // yet when the Cancelled state was entered).
            Mux()->Invoke(nullptr);
        } else if (InState(State::kNotStarted) && !awaitable_.await_ready()) {
            // Awaitable was not needed. await_ready() would not have returned
            // true unless it could be skipped, which we assert here; Cancel()
            // will call Mux()->Invoke(nullptr).
            [[maybe_unused]] const bool cancelled = Cancel();
            DCHECK_F(cancelled);
        } else {
            // Awaitable is ready; get its result. reportResult() will call
            // mux()->invoke().
            DCHECK_F(InState(State::kNotStarted) || InState(State::kReady));
            ReportResult();
        }
    }

    auto Result() && -> Ret
    {
        DCHECK_F(InState(State::kSucceeded));
        return Storage<Ret>::Unwrap(
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            std::move(*reinterpret_cast<StorageType*>(storage_)));
    }

    auto AsOptional() && -> Optional<Ret>
    {
        switch (GetState()) {
        case State::kSucceeded:
            return Storage<Ret>::Unwrap(
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                std::move(*reinterpret_cast<StorageType*>(storage_)));
        case State::kCancelled:
            return std::nullopt;
        case State::kNotStarted:
        case State::kCancellationPending:
        case State::kRunning:
        case State::kCancelling:
        case State::kReady:
        case State::kFailed:
            Unreachable();
        }
        Unreachable();
    }

    [[nodiscard]] auto MustResume() const noexcept -> bool
    {
        // This is called from the mux await_must_resume(), which runs in two
        // situations:
        //
        // - After parent resumption when a past await_cancel() or
        //   await_early_cancel() didn't complete synchronously: this occurs
        //   after every awaitable in the mux has resumed its parent, and the
        //   outcome of each awaitable (cancelled vs completed) was already
        //   decided in Invoke().
        //
        // - After await_early_cancel() returned false but await_ready()
        //   returned true, with no suspension involved: no one has called
        //   await_must_resume() yet, so we shall. To avoid calling
        //   await_must_resume() multiple times, we will check it here and
        //   change CancellationPending to either Cancelled or Ready, and then
        //   ReportImmediateResult() will know which path to take.

        switch (GetState()) {
        // No-suspension-yet cases, ReportImmediateResult() about to be invoked:
        case State::kNotStarted:
            return true;
        case State::kCancellationPending: {
            bool should_resume = awaitable_.await_must_resume();
            // NOLINTNEXTLINE(*-pro-type-const-cast)
            const_cast<MuxHelper*>(this)->SetState(
                should_resume ? State::kReady : State::kCancelled);
            return should_resume;
        }

        // Could be either path:
        case State::kCancelled:
            return false;

        // Already-suspended-and-resumed cases:
        case State::kReady:
        case State::kSucceeded:
        case State::kFailed:
            return true;

        case State::kRunning:
        case State::kCancelling:
            Unreachable();
        }
        Unreachable();
    }

private:
    [[nodiscard]] auto Mux() const noexcept -> MuxT* { return mux_.Ptr(); }
    void SetState(State st) noexcept { mux_.Set(Mux(), st); }

    void KickOff()
    {
        // Called from Suspend(); state is NotStarted or CancellationPending
        bool cancel_requested = InState(State::kCancellationPending);
        SetState(cancel_requested ? State::kCancelling : State::kRunning);
        if (awaitable_.await_ready()) {
            Invoke();
        } else {
            resume_fn = +[](CoroutineFrame* frame) {
                static_cast<Self*>(frame)->Invoke();
            };
            try {
                awaitable_.await_suspend(this->ToHandle()).resume();
            } catch (...) {
                std::exception_ptr ex = std::current_exception();
                DCHECK_NOTNULL_F(ex, "foreign exceptions and forced unwinds are not supported");
                SetState(State::kFailed);
                Mux()->Invoke(ex);
            }
        }
    }

    void ReportResult()
    {
        std::exception_ptr ex = nullptr;
        try {
            SetState(State::kSucceeded);
            new (storage_)
                StorageType(Storage<Ret>::Wrap(awaitable_.await_resume()));
        } catch (...) {
            SetState(State::kFailed);
            ex = std::current_exception();
            DCHECK_NOTNULL_F(
                ex, "foreign exceptions and forced unwinds are not supported");
        }
        Mux()->Invoke(ex);
    }

    void Invoke()
    {
        switch (GetState()) {
        case State::kCancelling:
            if (!awaitable_.await_must_resume()) {
                SetState(State::kCancelled);
                Mux()->Invoke(nullptr);
                return;
            }
            [[fallthrough]];
        case State::kRunning:
        case State::kReady:
            ReportResult();
            break;
        case State::kCancellationPending:
        case State::kNotStarted:
        case State::kCancelled:
        case State::kSucceeded:
        case State::kFailed:
            Unreachable();
        }
    }

    // Note: we cannot use alignof(MuxT) here because MuxT is not complete
    // defined yet, so use `coroutine_handle` for alignment.
    PointerBits<MuxT, State, kStateWidth, alignof(Handle)> mux_ {};

    SanitizedAwaiter<Aw> awaitable_ {};
    // NOLINTNEXTLINE(*-avoid-c-arrays)
    alignas(StorageType) char storage_[sizeof(StorageType)] {};
};

} // namespace oxygen::co::detail
