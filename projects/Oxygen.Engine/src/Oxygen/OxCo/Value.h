//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/IntrusiveList.h>

namespace oxygen::co {

//! A variable that can wake tasks when its value changes.
/*!
 Allows suspending a task until the value of the variable, or a transition
 thereof, satisfies a predicate.
*/
template <class T>
class Value {
    class AwaitableBase;
    template <class Fn>
    class UntilMatchesAwaitable;
    template <class Fn>
    class UntilChangedAwaitable;

public:
    Value() = default;
    explicit Value(T value)
        : value_(std::move(value))
    {
    }
    ~Value() = default;

    Value(Value&&) = delete;
    auto operator=(Value&&) -> Value& = delete;

    OXYGEN_DEFAULT_COPYABLE(Value)

    [[nodiscard]] auto Get() noexcept -> T& { return value_; }

    [[nodiscard]] auto Get() const noexcept -> const T& { return value_; }
    // ReSharper disable once CppNonExplicitConversionOperator
    // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
    operator const T&() const noexcept { return value_; }

    void Set(T value);
    auto operator=(T value) -> Value&;

    //! Suspends the caller until the stored value matches the predicate.
    //! (or resumes it immediately if it already does).
    /*!
     Yielded value will match the predicate, even though the value
     stored in the class may have changed since the caller was scheduled
     to resume.
    */
    template <std::invocable<const T&> Fn>
    auto UntilMatches(Fn&& predicate) -> Awaitable<T> auto;

    //! Suspends the caller until the stored value matches the expected one.
    //! (or resumes it immediately if it already does).
    auto UntilEquals(T expected) -> Awaitable<T> auto
    {
        return UntilMatches([expected = std::move(expected)](const T& value) {
            return value == expected;
        });
    }

    //! Suspends the caller until the transition of the stored value
    //! matches the predicate.
    /*!
     The predicate will be tested on further each assignment, including an
     assignment of an already stored value.

     Yields a pair of the previous and the current value that matched the
     predicate, even though the value stored in the class may have changed since
     the caller was scheduled to resume.
    */
    template <std::invocable<const T& /*from*/, const T& /*to*/> Fn>
    auto UntilChanged(Fn&& predicate) -> Awaitable<std::pair<T, T>> auto;

    //! Waits for any nontrivial transition (change from `x` to `y` where `x != y`).
    auto UntilChanged() -> Awaitable<std::pair<T, T>> auto
    {
        return UntilChanged(
            [](const T& from, const T& to) { return from != to; });
    }

    //! Waits for a transition from `from` to `to`.
    auto UntilChanged(T from, T to) -> Awaitable<std::pair<T, T>> auto
    {
        return UntilChanged(
            [from = std::move(from), to = std::move(to)](const T& f, const T& t) {
                return f == from && t == to;
            });
    }

private:
    T value_;
    detail::IntrusiveList<AwaitableBase> parked_;
};

//
// Implementation
//

template <class T>
class Value<T>::AwaitableBase
    : public detail::IntrusiveListItem<AwaitableBase> {
public:
    void await_suspend(const detail::Handle h)
    {
        handle_ = h;
        cond_.parked_.PushBack(*this);
    }

    auto await_cancel(detail::Handle /*unused*/) noexcept
    {
        this->Unlink();
        return std::true_type {};
    }

protected:
    explicit AwaitableBase(Value& cond)
        : cond_(cond)
    {
    }

    void Park() { cond_.parked_.PushBack(*this); }
    void DoResume() const { handle_.resume(); }

    [[nodiscard]] auto GetValue() const noexcept -> const T& { return cond_.value_; }

private:
    virtual void OnChanged(const T& from, const T& to) = 0;

    Value& cond_; // NOLINT(*-avoid-const-or-ref-data-members)
    detail::Handle handle_;
    friend Value;
};

template <class T>
template <class Fn>
class Value<T>::UntilMatchesAwaitable final : public AwaitableBase {
public:
    UntilMatchesAwaitable(Value& cond, Fn fn)
        : AwaitableBase(cond)
        , fn_(std::move(fn))
    {
        if (fn_(cond.value_)) {
            result_ = cond.value_;
        }
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return result_.has_value(); }
    auto await_resume() && -> T { return std::move(*result_); }

private:
    void OnChanged(const T& /*from*/, const T& to) override
    {
        if (fn_(to)) {
            result_ = to;
            this->DoResume();
        } else {
            this->Park();
        }
    }

    [[no_unique_address]] Fn fn_;
    std::optional<T> result_;
};

template <class T>
template <class Fn>
class Value<T>::UntilChangedAwaitable final : public AwaitableBase {
public:
    explicit UntilChangedAwaitable(Value& cond, Fn fn)
        : AwaitableBase(cond)
        , fn_(std::move(fn))
    {
    }

    // ReSharper disable CppMemberFunctionMayBeStatic
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
    auto await_resume() && -> std::pair<T, T> { return std::move(*result_); }
    // ReSharper restore CppMemberFunctionMayBeStatic

private:
    void OnChanged(const T& from, const T& to) override
    {
        if (fn_(from, to)) {
            result_ = std::make_pair(from, to);
            this->DoResume();
        } else {
            this->Park();
        }
    }

    [[no_unique_address]] Fn fn_;
    std::optional<std::pair<T, T>> result_;
};

template <class T>
void Value<T>::Set(T value)
{
    T prev = std::exchange(value_, value);
    auto parked = std::move(parked_);
    while (!parked.Empty()) {
        auto& p = parked.Front();
        parked.PopFront();

        // Note: not using `value_` here; if `Set()` is called outside
        // of a task, then `OnChanged()` will immediately resume the
        // awaiting tasks, which could cause `value_` to change further.
        p.OnChanged(prev, value);
    }
}

template <class T>
auto Value<T>::operator=(T value) -> Value&
{
    Set(std::move(value));
    return *this;
}

template <class T>
template <std::invocable<const T&> Fn>
auto Value<T>::UntilMatches(Fn&& predicate) -> Awaitable<T> auto
{
    return UntilMatchesAwaitable<Fn>(*this, std::forward<Fn>(predicate));
}

template <class T>
template <std::invocable<const T& /*from*/, const T& /*to*/> Fn>
auto Value<T>::UntilChanged(Fn&& predicate) -> Awaitable<std::pair<T, T>> auto
{
    return UntilChangedAwaitable<Fn>(*this, std::forward<Fn>(predicate));
}

} // namespace oxygen::co
