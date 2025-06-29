//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/ProxyFrame.h>
#include <Oxygen/OxCo/Detail/Result.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>

namespace oxygen::co::detail {

template <Awaitable First, class ThenFn>
class Sequence : /*private*/ ProxyFrame {

  static auto GetSecond(ThenFn& fn, AwaitableReturnType<First>& first)
    -> decltype(auto)
  {
    if constexpr (requires { fn(std::move(first)); }) {
      return fn(std::move(first));
    } else if constexpr (requires { fn(first); }) {
      return fn(first);
    } else {
      return fn();
    }
  }

  using Second = decltype(GetSecond(
    std::declval<ThenFn&>(), std::declval<AwaitableReturnType<First>&>()));

public:
  Sequence(First first, ThenFn then_fn)
    : first_(std::move(first))
    , then_fn_(std::move(then_fn))
  {
  }

  ~Sequence() = default;

  OXYGEN_MAKE_NON_MOVABLE(Sequence)
  OXYGEN_MAKE_NON_COPYABLE(Sequence)

  // ReSharper disable CppMemberFunctionMayBeStatic
  auto await_ready() const noexcept -> bool { return false; }

  void await_set_executor(Executor* e) noexcept
  {
    second_ = e;
    first_.awaiter.await_set_executor(e);
  }

  auto await_early_cancel() noexcept
  {
    cancelling_ = true;
    return first_.awaiter.await_early_cancel();
  }

  void await_suspend(const Handle h)
  {
    DLOG_F(5, "   ...sequence {} yielding to...", fmt::ptr(this));
    parent_ = h;
    if (first_.awaiter.await_ready()) {
      KickOffSecond();
    } else {
      this->resume_fn = +[](CoroutineFrame* frame) {
        auto* self = static_cast<Sequence*>(frame);
        self->KickOffSecond();
      };
      first_.awaiter.await_suspend(this->ToHandle()).resume();
    }
  }

  auto await_cancel(Handle h) noexcept -> bool
  {
    DLOG_F(5, "sequence {} ({} stage) cancellation requested", fmt::ptr(this),
      InFirstStage() ? "first" : "second");
    cancelling_ = true;
    if (InFirstStage()) {
      return first_.awaiter.await_cancel(this->ToHandle());
    }
    if (InSecondStage()) {
      return second().awaiter.await_cancel(h);
    }
    return false; // will carry out cancellation later
  }

  auto await_must_resume() const noexcept -> bool
  {
    // Note that await_must_resume() is called by our parent when we
    // resume them after a cancellation that did not complete synchronously.
    // To understand the logic in this method, consider all the places
    // where we call parent_.resume(). In particular, if we're still
    // inFirstStage(), we must have hit the cancellation check at the
    // beginning of kickOffSecond(), which means we've already verified
    // that the first stage await_must_resume() returned false, and we
    // should return false here without consulting the awaitable further.
    // Similarly, if we're in neither the first nor the second stage,
    // the second stage must have completed via early cancellation.
    const bool ret = std::holds_alternative<std::exception_ptr>(second_)
      || (InSecondStage() && second().awaiter.await_must_resume());
    if (!ret && InSecondStage()) {
      // Destroy the second stage, which will release any resources
      // it might have held
      second_.template emplace<std::monostate>();
    }
    return ret;
  }

  auto await_resume() -> decltype(auto)
  {
    ScopeGuard guard([this]() noexcept {
      // Destroy the second stage and the return value of the first stage
      second_.template emplace<std::monostate>();
    });

    if (auto ex = std::get_if<std::exception_ptr>(&second_)) {
      std::rethrow_exception(*ex);
    }
    return second().awaiter.await_resume();
  }
  // ReSharper restore CppMemberFunctionMayBeStatic

private:
  struct FirstStage {
    [[no_unique_address]] First awaitable;
    [[no_unique_address]] SanitizedAwaiter<First> awaiter;

    explicit FirstStage(First&& aw)
      : awaitable(std::forward<First>(aw))
      , awaiter(std::forward<First>(awaitable))
    {
    }
  };

  // Explicitly provide a template argument, so immediate awaitables
  // would resolve to Second&& instead of Second.
  // For the same reason, don't use AwaiterType<> here.
  using SecondAwaiter = decltype(GetAwaiter<Second&&>(std::declval<Second>()));

  struct SecondStage {
    [[no_unique_address]] AwaitableReturnType<First> first_value;
    [[no_unique_address]] Second awaitable;
    [[no_unique_address]] SanitizedAwaiter<Second&&, SecondAwaiter> awaiter;

    explicit SecondStage(Sequence* c)
      : first_value(std::move(c->first_.awaiter).await_resume())
      , awaitable(GetSecond(c->then_fn_, first_value))
      , awaiter(std::forward<Second>(awaitable))
    {
    }
  };

  auto InFirstStage() const noexcept -> bool
  {
    return std::holds_alternative<Executor*>(second_);
  }

  auto InSecondStage() const noexcept -> bool
  {
    return std::holds_alternative<SecondStage>(second_);
  }

  auto second() noexcept -> SecondStage&
  {
    return std::get<SecondStage>(second_);
  }

  auto second() const noexcept -> const SecondStage&
  {
    return std::get<SecondStage>(second_);
  }

  void KickOffSecond() noexcept
  {
    if (cancelling_ && !first_.awaiter.await_must_resume()) {
      DLOG_F(5,
        "sequence {} (cancelling) first stage completed, "
        "confirming cancellation",
        fmt::ptr(this));
      parent_.resume();
      return;
    }

    DLOG_F(5, "sequence {}{} first stage completed, continuing with...",
      fmt::ptr(this), cancelling_ ? " (cancelling)" : "");
    DCHECK_F(InFirstStage());
    Executor* ex = std::get<Executor*>(second_);

    // Mark first stage as completed
    // (this is necessary if thenFn_() attempts to cancel us)
    second_.template emplace<std::monostate>();

    try {
      second_.template emplace<SecondStage>(this);
    } catch (...) {
      second_.template emplace<std::exception_ptr>(std::current_exception());
      parent_.resume();
      return;
    }

    if (cancelling_) {
      if (second().awaiter.await_early_cancel()) {
        second_.template emplace<std::monostate>();

        parent_.resume();
        return;
      }
    }

    if (second().awaiter.await_ready()) {
      parent_.resume();
    } else {
      second().awaiter.await_set_executor(ex);
      second().awaiter.await_suspend(parent_).resume();
    }
  }

private:
  Handle parent_;
  [[no_unique_address]] FirstStage first_;

  [[no_unique_address]] ThenFn then_fn_;
  mutable std::variant<Executor*, // running first stage
    SecondStage, // running second stage,
    std::monostate, // running neither (either constructing
                    // second stage, or it confirmed early
                    // cancellation)
    std::exception_ptr> // first stage threw an exception
    second_;
  bool cancelling_ = false;
};

template <class ThenFn> class SequenceBuilder {
public:
  explicit SequenceBuilder(ThenFn fn)
    : fn_(std::move(fn))
  {
  }

  template <Awaitable First>
    requires(std::invocable<ThenFn, AwaitableReturnType<First>&>
      || std::invocable<ThenFn, AwaitableReturnType<First> &&>
      || std::invocable<ThenFn>)
  // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) perfect forwarding
  friend auto operator|(First&& first, SequenceBuilder&& builder)
  {
    return MakeAwaitable<Sequence<First, ThenFn>, First, ThenFn>(
      std::forward<First>(first), std::move(builder.fn_));
  }

  // Allow right associativity of SequenceBuilder's
  template <class ThirdFn> auto operator|(SequenceBuilder<ThirdFn>&& next) &&
  {
    return detail::SequenceBuilder(
      [fn = std::move(fn_), next = std::move(next)]<class T>(
        T&& value) mutable {
        return fn(std::forward<T>(value)) | std::move(next);
      });
  }

private:
  ThenFn fn_;
};

} // namespace oxygen::co::detail
