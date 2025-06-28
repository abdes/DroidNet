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
template <class T> class Value {
  class AwaiterBase;
  template <class Fn> class UntilMatchesAwaiter;
  template <class Fn> class UntilChangedAwaiter;

  template <class Fn> class Comparison;

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

  //! Runs `fn` on a stored value (which can modify it in-place),
  //! then wakes up awaiters as appropriate.
  /*!
   Returns the modified value (which may be different from the stored
   one if any immediately resumed awaiters modified it further).
  */
  T Modify(std::invocable<T&> auto&& fn);

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
    return UntilMatches([expected = std::move(expected)](
                          const T& value) { return value == expected; });
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

  //! Waits for any nontrivial transition (change from `x` to `y` where `x !=
  //! y`).
  auto UntilChanged() -> Awaitable<std::pair<T, T>> auto
  {
    return UntilChanged([](const T& from, const T& to) { return from != to; });
  }

  //! Waits for a transition from `from` to `to`.
  auto UntilChanged(T from, T to) -> Awaitable<std::pair<T, T>> auto
  {
    return UntilChanged([from = std::move(from), to = std::move(to)](const T& f,
                          const T& t) { return f == from && t == to; });
  }

  // Shorthands for comparison operations.
  // Each of these yields an object which is convertible to bool,
  // but also can yield an awaitable through a friend `until()` function:
  //
  //     corral::Value<int> v;
  //     bool b = (v >= 42);  // works
  //     co_await until(v >= 42);  // also works
  //
  // Note that unlike `untilMatches()` above, such awaitables do not yield
  // the value which triggered the resumption.
#define OXCO_DEFINE_COMPARISON_OP(op)                                          \
  template <class U>                                                           \
    requires(requires(const T t, const U u) {                                  \
      { t op u } -> std::convertible_to<bool>;                                 \
    })                                                                         \
  auto operator op(U&& u)                                                      \
  {                                                                            \
    return MakeComparison([u](const T& t) { return t op u; });                 \
  }                                                                            \
                                                                               \
  template <class U>                                                           \
    requires(requires(const T t, const U u) {                                  \
      { t op u } -> std::convertible_to<bool>;                                 \
    })                                                                         \
  bool operator op(U&& u) const                                                \
  {                                                                            \
    return value_ op std::forward<U>(u);                                       \
  }

  OXCO_DEFINE_COMPARISON_OP(==)
  OXCO_DEFINE_COMPARISON_OP(!=)
  OXCO_DEFINE_COMPARISON_OP(<)
  OXCO_DEFINE_COMPARISON_OP(<=)
  OXCO_DEFINE_COMPARISON_OP(>)
  OXCO_DEFINE_COMPARISON_OP(>=)
#undef OXCO_DEFINE_COMPARISON_OP

  template <class U>
    requires(requires(const T t, const U u) { t <=> u; })
  auto operator<=>(U&& rhs) const
  {
    return value_ <=> std::forward<U>(rhs);
  }

  //
  // Shorthands proxying arithmetic operations to the stored value.
  //

  T operator++()
    requires(requires(T t) { ++t; })
  {
    return Modify([](T& v) { ++v; });
  }

  T operator++(int)
    requires(requires(T t) { t++; })
  {
    auto ret = value_;
    Modify([](T& v) { ++v; });
    return ret;
  }

  T operator--()
    requires(requires(T t) { --t; })
  {
    return Modify([](T& v) { --v; });
  }

  T operator--(int)
    requires(requires(T t) { t--; })
  {
    auto ret = value_;
    Modify([](T& v) { --v; });
    return ret;
  }

#define OXCO_DEFINE_ARITHMETIC_OP(op)                                          \
  template <class U>                                                           \
  T operator op(U&& rhs)                                                       \
    requires(requires(T t, U u) { t op u; })                                   \
  {                                                                            \
    return Modify([&rhs](T& v) { v op std::forward<U>(rhs); });                \
  }

  OXCO_DEFINE_ARITHMETIC_OP(+=)
  OXCO_DEFINE_ARITHMETIC_OP(-=)
  OXCO_DEFINE_ARITHMETIC_OP(*=)
  OXCO_DEFINE_ARITHMETIC_OP(/=)
  OXCO_DEFINE_ARITHMETIC_OP(%=)
  OXCO_DEFINE_ARITHMETIC_OP(&=)
  OXCO_DEFINE_ARITHMETIC_OP(|=)
  OXCO_DEFINE_ARITHMETIC_OP(^=)
  OXCO_DEFINE_ARITHMETIC_OP(<<=)
  OXCO_DEFINE_ARITHMETIC_OP(>>=)

#undef OXCO_DEFINE_ARITHMETIC_OP

private:
  template <class Fn> Comparison<Fn> MakeComparison(Fn&& fn)
  {
    // gcc-14 fails to CTAD Comparison signature here,
    // so wrap its construction into a helper function.
    return Comparison<Fn>(*this, std::forward<Fn>(fn));
  }

private:
  T value_;
  detail::IntrusiveList<AwaiterBase> parked_;
};

//
// Implementation
//

template <class T>
class Value<T>::AwaiterBase : public detail::IntrusiveListItem<AwaiterBase> {
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
  explicit AwaiterBase(Value& cond)
    : cond_(cond)
  {
  }

  void Park() { cond_.parked_.PushBack(*this); }

  [[nodiscard]] auto GetValue() const noexcept -> const T&
  {
    return cond_.value_;
  }

private:
  virtual bool Matches(const T& from, const T& to) = 0;

  virtual void OnChanged(const T& from, const T& to)
  {
    if (Matches(from, to)) {
      handle_.resume();
    } else {
      Park();
    }
  }

  Value& cond_; // NOLINT(*-avoid-const-or-ref-data-members)
  detail::Handle handle_;
  friend Value;
};

template <class T>
template <class Fn>
class Value<T>::UntilMatchesAwaiter final : public AwaiterBase {
public:
  UntilMatchesAwaiter(Value& cond, Fn fn)
    : AwaiterBase(cond)
    , fn_(std::move(fn))
  {
    if (fn_(cond.value_)) {
      result_ = cond.value_;
    }
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool
  {
    return result_.has_value();
  }
  auto await_resume() && -> T { return std::move(*result_); }

private:
  auto Matches(const T& /*from*/, const T& to) -> bool override
  {
    if (fn_(to)) {
      result_ = to;
      return true;
    } else {
      return false;
    }
  }

  [[no_unique_address]] Fn fn_;
  std::optional<T> result_;
};

template <class T> template <class Fn> class Value<T>::Comparison {
  class Awaiter : public AwaiterBase {
  public:
    Awaiter(Value& cond, Fn fn)
      : AwaiterBase(cond)
      , fn_(std::move(fn))
    {
    }
    bool await_ready() const noexcept { return fn_(this->GetValue()); }
    void await_resume() { }

  private:
    bool Matches(const T& /*from*/, const T& to) override { return fn_(to); }

  private:
    Fn fn_;
  };

public:
  Comparison(Value& cond, Fn fn)
    : cond_(cond)
    , fn_(std::move(fn))
  {
  }

  operator bool() const noexcept { return fn_(cond_.value_); }

  friend Awaiter Until(Comparison&& self)
  {
    return Awaiter(self.cond_, std::move(self.fn_));
  }

private:
  Value& cond_;
  Fn fn_;
};

template <class T>
template <class Fn>
class Value<T>::UntilChangedAwaiter final : public AwaiterBase {
public:
  explicit UntilChangedAwaiter(Value& cond, Fn fn)
    : AwaiterBase(cond)
    , fn_(std::move(fn))
  {
  }

  // ReSharper disable CppMemberFunctionMayBeStatic
  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
  auto await_resume() && -> std::pair<T, T> { return std::move(*result_); }
  // ReSharper restore CppMemberFunctionMayBeStatic

private:
  bool Matches(const T& from, const T& to) override
  {
    if (fn_(from, to)) {
      result_ = std::make_pair(from, to);
      return true;
    } else {
      return false;
    }
  }

  [[no_unique_address]] Fn fn_;
  std::optional<std::pair<T, T>> result_;
};

template <class T> T Value<T>::Modify(std::invocable<T&> auto&& fn)
{
  T prev = value_;
  std::forward<decltype(fn)>(fn)(value_);
  T value = value_;
  auto parked = std::move(parked_);
  while (!parked.Empty()) {
    auto& p = parked.Front();
    parked.PopFront();

    // Note: not using `value_` here; if `Set()` is called outside
    // of a task, then `OnChanged()` will immediately resume the
    // awaiting tasks, which could cause `value_` to change further.
    p.OnChanged(prev, value);
  }
  return value;
}

template <class T> void Value<T>::Set(T value)
{
  Modify([&](T& v) { v = std::move(value); });
}

template <class T> auto Value<T>::operator=(T value) -> Value&
{
  Set(std::move(value));
  return *this;
}

template <class T>
template <std::invocable<const T&> Fn>
auto Value<T>::UntilMatches(Fn&& predicate) -> Awaitable<T> auto
{
  return UntilMatchesAwaiter<Fn>(*this, std::forward<Fn>(predicate));
}

template <class T>
template <std::invocable<const T& /*from*/, const T& /*to*/> Fn>
auto Value<T>::UntilChanged(Fn&& predicate) -> Awaitable<std::pair<T, T>> auto
{
  return UntilChangedAwaiter<Fn>(*this, std::forward<Fn>(predicate));
}

} // namespace oxygen::co
