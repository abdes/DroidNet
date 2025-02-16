//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/GetAwaiter.h>
#include <Oxygen/OxCo/Detail/IntrusiveList.h>
#include <Oxygen/OxCo/Detail/IntrusivePtr.h>
#include <Oxygen/OxCo/Detail/ProxyFrame.h>
#include <Oxygen/OxCo/Detail/Result.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>

namespace oxygen::co {

//! Models a shared asynchronous operation: an awaitable of type `Awaitable` that
//! can be awaited multiple times in parallel, modeling a task with multiple
//! parents.
/*!
 The result of the operation will be propagated to each of its parents, and the
 shared operation will be cancelled if all of its parents are cancelled.

 Cancellation of a parent while other parents remain always succeeds.
 Cancellation of the last parent becomes cancellation of the shared task, and
 may complete asynchronously or fail if the shared task can handle cancellation
 in those ways.

 `Shared` is copyable, and copies reference the same underlying task. There is
 no difference between running co_await one time on each of N copies, and
 running co_await N times on one copy.

 New parents that attempt to join after the shared task has been cancelled (due
 to cancellation of all the parents in the initial batch) will see a
 `std::runtime_error` explaining that the value is unavailable due to
 cancellation; this can also be tested explicitly using the `Closed()` method.

 Behavior is undefined if the shared operation indirectly attempts to await
 itself. If this occurs, it is possible for cancellations to result in the
 shared task being the only thing keeping itself alive, which will cause a
 resource leak or worse.
*/
template <class Awaitable>
class Shared {
    using WrappedAwaiter = detail::AwaiterType<Awaitable&>;
    using ReturnType = decltype(std::declval<WrappedAwaiter>().await_resume());
    using ConstRef = std::add_lvalue_reference_t<const ReturnType>;
    using Storage = detail::Storage<ReturnType>;

    class State;
    class Awaiter;

public:
    Shared() = default;
    explicit Shared(Awaitable&& obj);
    template <class... Args>
        requires(std::is_constructible_v<Awaitable, Args...>)
    explicit Shared(std::in_place_t tag, Args&&... args);

    [[nodiscard]] auto Get() const -> Awaitable*;
    explicit operator bool() const { return !state_; }
    auto operator*() const -> Awaitable& { return *Get(); }
    auto operator->() const -> Awaitable* { return Get(); }

    [[nodiscard]] auto Closed() const noexcept -> bool;
    [[nodiscard]] auto Done() const noexcept -> bool;

    auto operator co_await() -> co::Awaiter auto;

private:
    detail::IntrusivePtr<State> state_;
};

/// Awaitable object used for a single co_await on a shared task
template <class Awaitable>
class Shared<Awaitable>::Awaiter : public detail::IntrusiveListItem<Awaiter> {
public:
    explicit Awaiter(detail::IntrusivePtr<State> state)
        : state_(std::move(state))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool;
    auto await_early_cancel() noexcept;
    void await_set_executor(Executor* ex) noexcept;
    auto await_suspend(detail::Handle h) -> detail::Handle;
    auto await_cancel(detail::Handle h) noexcept;
    [[nodiscard]] auto await_must_resume() const noexcept;
    auto await_resume() -> ConstRef;

private:
    void WakeUp()
    {
        this->Unlink();
        std::exchange(parent_, std::noop_coroutine())();
    }
    friend class State;

    // The shared task state. If null, this awaitable is not associated with any
    // shared task; this can occur when awaiting a moved-from Shared, or after
    // cancellation of an awaitable that is not the last one for its task.
    detail::IntrusivePtr<State> state_;
    detail::Handle parent_;
};

//
// Implementation
//

/// Storage and lifetime management for the shared task underlying a Shared<T>
template <class Awaitable>
class Shared<Awaitable>::State : /*private*/ detail::ProxyFrame,
                                 public detail::RefCounted<State> {
public:
    template <class... Args>
    explicit State(Args&&... args);
    auto Get() -> Awaitable* { return &awaiter_; }
    [[nodiscard]] auto Closed() const noexcept -> bool
    {
        return result_.index() >= Cancelling;
    }
    [[nodiscard]] auto Done() const noexcept -> bool
    {
        return result_.index() <= Value;
    }
    [[nodiscard]] auto Ready() const noexcept -> bool;
    auto EarlyCancel(Awaiter* ptr) noexcept;
    void SetExecutor(Executor* ex) noexcept;
    auto Suspend(Awaiter* ptr) -> detail::Handle;
    auto Cancel(Awaiter* ptr) noexcept;
    auto MustResume() const noexcept;
    auto GetResult() -> ConstRef;

private:
    void Invoke();
    static void Trampoline(CoroutineFrame* frame)
    {
        static_cast<State*>(frame)->Invoke();
    }

    [[no_unique_address]] Awaitable awaitable_;
    detail::SanitizedAwaiter<Awaitable&> awaiter_;
    detail::IntrusiveList<Awaiter> parents_;
    std::variant<std::monostate, std::monostate, typename Storage::Type,
        std::exception_ptr, std::monostate, std::monostate>
        result_;

    // Indices of types stored in the variant
    static constexpr int Incomplete = 0;
    static constexpr int CancelPending = 1;
    static constexpr int Value = 2;
    static constexpr int Exception = 3;
    static constexpr int Cancelling = 4;
    static constexpr int Cancelled = 5;
};

template <class Awaitable>
template <class... Args>
Shared<Awaitable>::State::State(Args&&... args)
    : awaitable_(std::forward<Args>(args)...)
    , awaiter_(awaitable_)
{
    this->resume_fn = &State::Trampoline;
}

template <class Awaitable>
void Shared<Awaitable>::State::SetExecutor(Executor* ex) noexcept
{
    if (parents_.Empty()) {
        awaiter_.await_set_executor(ex);
    }
}

template <class Awaitable>
auto Shared<Awaitable>::State::Ready() const noexcept -> bool
{
    // If we already have some parents, make sure new arrivals don't
    // bypass the queue and try to call result() before the operation
    // officially completes; it's possible that ready() will become true
    // before the handle passed to suspend() is resumed.
    return result_.index() != Incomplete || (parents_.Empty() && awaiter_.await_ready());
}

template <class Awaitable>
auto Shared<Awaitable>::State::EarlyCancel(Awaiter* ptr) noexcept
{
    // The first arriving parent is considered to be responsible for
    // forwarding early cancellation to the shared task. Any parent
    // that arrives after it can safely be skipped without affecting the
    // supervision of the task. If the task already completed, then
    // we know we're not the first arrival, even if there are no
    // parents still registered; one of the previous parents must
    // have retrieved the task's result.
    if (parents_.Empty() && result_.index() == Incomplete) {
        // Forward early-cancel request to the shared task
        auto syncEarlyCancelled = awaiter_.await_early_cancel();
        if (syncEarlyCancelled) {
            result_.template emplace<Cancelled>();
            ptr->state_ = nullptr;
        } else {
            result_.template emplace<CancelPending>();
        }
        return syncEarlyCancelled;
    }

    // Skip this parent without affecting the shared task.
    // Match the return type of 'return syncEarlyCancelled;' above.
    ptr->state_ = nullptr;
    if constexpr (detail::Skippable<WrappedAwaiter>) {
        return std::true_type {};
    } else {
        return true;
    }
}

template <class Awaitable>
auto Shared<Awaitable>::State::Suspend(Awaiter* ptr) -> detail::Handle
{
    DLOG_F(1, "    ...on shared awaitable {} (holding {})", fmt::ptr(this),
        fmt::ptr(&awaiter_));
    const bool is_first = parents_.Empty();
    parents_.PushBack(*ptr);
    if (is_first) {
        // Taking an async backtrace from within a shared task will show its
        // oldest un-cancelled parent as the caller.
        ProxyFrame::LinkTo(ptr->parent_);
        if (result_.index() == CancelPending) {
            result_.template emplace<Cancelling>();
        }

        try {
            return awaiter_.await_suspend(this->ToHandle());
        } catch (...) {
            auto ex = std::current_exception();
            DCHECK_NOTNULL_F(
                ex, "foreign exceptions and forced unwinds are not supported");
            result_.template emplace<Exception>(std::move(ex));
            Invoke();
            return std::noop_coroutine(); // already woke up
        }
    }
    return std::noop_coroutine();
}

template <class Awaitable>
auto Shared<Awaitable>::State::GetResult() -> ConstRef
{
    // We can get here with result == CancelPending if early-cancel returned
    // false and the awaitable was then immediately ready. mustResume()
    // was checked already, so treat CancelPending like Incomplete.
    if (result_.index() == Incomplete || result_.index() == CancelPending) {
        try {
            if constexpr (std::is_same_v<ReturnType, void>) {
                std::move(awaiter_).await_resume();
                result_.template emplace<Value>();
            } else {
                result_.template emplace<Value>(
                    Storage::Wrap(std::move(awaiter_).await_resume()));
            }
        } catch (...) {
            result_.template emplace<Exception>(std::current_exception());
        }
    }

    if (result_.index() == Value) [[likely]] {
        return Storage::UnwrapCRef(std::get<Value>(result_));
    }
    if (result_.index() == Exception) {
        std::rethrow_exception(std::get<Exception>(result_));
    }
    // We get here if a new parent tries to join the shared operation
    // after all of its existing parents were cancelled and
    // thus the shared task was cancelled. The new parent never
    // called suspend() so we don't have to worry about removing
    // it from the list of parents. We can't propagate the
    // cancellation in a different context than the context that
    // was cancelled, so we throw an exception instead.
    throw std::runtime_error(
        "Shared task was cancelled because all of its parent "
        "tasks were previously cancelled, so there is no "
        "value for new arrivals to retrieve");
}

template <class Awaitable>
auto Shared<Awaitable>::State::Cancel(Awaiter* ptr) noexcept
{
    if (parents_.ContainsOneItem()) {
        DLOG_F(1,
            "cancelling shared awaitable {} (holding {}); "
            "forwarding cancellation",
            fmt::ptr(this), fmt::ptr(&awaiter_));
        DCHECK_EQ_F(&parents_.Front(), ptr);
        // Prevent new parents from joining, and forward the cancellation
        // to the shared task
        result_.template emplace<Cancelling>();
        auto sync_cancelled = awaiter_.await_cancel(this->ToHandle());
        if (sync_cancelled) {
            result_.template emplace<Cancelled>();
            ptr->Unlink();
            ptr->state_ = nullptr;
        }
        return sync_cancelled;
    }
    // Note that we also get here if parents_ is empty, which can occur if
    // the resumption of one parent cancels another (imagine corral::anyOf()
    // on multiple copies of the same Shared<T>). We're still linked into
    // the list, it's just a local variable in invoke(). We'll let these
    // additional parents propagate cancellation and assume that the first
    // one will do a good enough job of carrying the value. This is
    // important to allow Shared<T> to be abortable/disposable if T is.
    DLOG_F(1,
        "cancelling shared awaitable {} (holding {}); "
        "dropping parent",
        fmt::ptr(this), fmt::ptr(&awaiter_));
    ptr->Unlink();
    ptr->state_ = nullptr;

    // If the cancelled parent was previously the first one, then we should
    // choose a new first one to avoid backtracking into something dangling.
    // (If we have no parents_, then the shared task has completed, so it
    // doesn't matter what it declares as its caller.)
    if (!parents_.Empty()) {
        ProxyFrame::LinkTo(parents_.Front().parent_);
    }

    // Match the type of 'return syncCancelled;' above:
    if constexpr (detail::Abortable<WrappedAwaiter>) {
        return std::true_type {};
    } else {
        return true;
    }
}

template <class Awaitable>
auto Shared<Awaitable>::State::MustResume() const noexcept
{
    // This is called after an individual parent's cancellation did not succeed
    // synchronously. Early cancellation of not-the-first parent, and regular
    // cancellation of not-the-last parent, always succeed synchronously and
    // will not enter this function.
    //
    // If regular cancellation of the last parent didn't succeed synchronously,
    // we would have set result_ to Cancelling and invoke() would have clarified
    // that to either Incomplete or Cancelled based on the underlying
    // await_must_resume() by the time we got here. We return true for
    // Incomplete to prompt a call to result() that will fill in the Value or
    // Exception.
    //
    // If early cancellation of the first parent didn't succeed synchronously,
    // we would have set result_ to CancelPending. suspend() transforms that to
    // Cancelling which feeds into the regular-cancel case above once the shared
    // task completes. But if the awaitable was immediately ready() after a
    // non-synchronous earlyCancel(), we get here with CancelPending still set,
    // and need to check the underlying await_must_resume().
    bool ret = result_.index() == CancelPending ? awaiter_.await_must_resume()
                                                : result_.index() != Cancelled;
    if constexpr (detail::CancelAlwaysSucceeds<WrappedAwaiter>) {
        DCHECK_EQ_F(ret, false);
        return std::false_type {};
    } else {
        return ret;
    }
}

template <class Awaitable>
void Shared<Awaitable>::State::Invoke()
{
    DLOG_F(1, "shared awaitable {} (holding {}) resumed", fmt::ptr(this),
        fmt::ptr(&awaiter_));
    if (result_.index() == Cancelling) {
        if (awaiter_.await_must_resume()) {
            result_.template emplace<Incomplete>();
        } else {
            result_.template emplace<Cancelled>();
        }
    }
    auto parents = std::move(parents_);
    while (!parents.Empty()) {
        parents.Front().WakeUp();
    }
}

template <class Awaitable>
void Shared<Awaitable>::Awaiter::await_set_executor(Executor* ex) noexcept
{
    if (state_) {
        state_->SetExecutor(ex);
    }
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_ready() const noexcept -> bool
{
    return !state_ || state_->Ready();
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_early_cancel() noexcept
{
    return state_ ? state_->EarlyCancel(this) : std::true_type {};
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_suspend(const detail::Handle h)
    -> detail::Handle
{
    parent_ = h;
    return state_->Suspend(this);
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_resume() -> ConstRef
{
    if (!state_) {
        if constexpr (std::is_same_v<ReturnType, void>) {
            return;
        } else {
            ABORT_F("co_await on an empty shared");
        }
    }
    return state_->GetResult();
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_cancel(detail::Handle /*h*/) noexcept
{
    return state_ ? state_->Cancel(this) : std::true_type {};
}

template <class Awaitable>
auto Shared<Awaitable>::Awaiter::await_must_resume() const noexcept
{
    return state_ ? state_->MustResume() : std::false_type {};
}

template <class Awaitable>
Shared<Awaitable>::Shared(
    Awaitable&& obj) // NOLINT(*-rvalue-reference-param-not-moved)
                     // Perfect forwarding
    : state_(new State(std::forward<Awaitable>(obj)))
{
}

template <class Awaitable>
template <class... Args>
    requires(std::is_constructible_v<Awaitable, Args...>)
Shared<Awaitable>::Shared(std::in_place_t /*tag*/, Args&&... args)
    : state_(new State(std::forward<Args>(args)...))
{
}

template <class Awaitable>
auto Shared<Awaitable>::Closed() const noexcept -> bool
{
    return state_ ? state_->Closed() : true;
}
template <class Awaitable>
auto Shared<Awaitable>::Done() const noexcept -> bool
{
    return state_ ? state_->Done() : true;
}

template <class Awaitable>
auto Shared<Awaitable>::Get() const -> Awaitable*
{
    return state_ ? state_->Get() : nullptr;
}

template <class Awaitable> //
auto Shared<Awaitable>::operator co_await() -> co::Awaitable auto
{
    return Awaiter(state_);
}

} // namespace oxygen::co
