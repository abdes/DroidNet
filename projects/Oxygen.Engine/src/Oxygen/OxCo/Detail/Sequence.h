//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <optional>
#include <variant>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/ProxyFrame.h>
#include <Oxygen/OxCo/Detail/Result.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>

namespace oxygen::co {
// Defined in Awaitables.h, which includes this header. Declared here so the
// call below is found by ordinary (two-phase) lookup on every compiler.
template <ImmediateAwaitable T, class... Args>
auto MakeAwaitable(Args... args) -> Awaitable auto;
} // namespace oxygen::co

namespace oxygen::co::detail {

//! Two-stage storage shared by `Sequence` and `With`.
/*!
 Holds a first awaitable, a callable producing the second awaitable from a
 transferred value of type `Xfer`, and the second stage once it exists.

 The first stage may be constructed after the object itself (see the
 `std::nullptr_t` constructor), because `With` must finish constructing before
 it invokes the callable producing its first stage.
*/
template <Awaitable First, class ThenFn, class Xfer>
class SequenceBase : protected ProxyFrame {
  static auto GetSecond(ThenFn& fn, ReturnType<Xfer>& xf) -> decltype(auto)
  {
    if constexpr (std::is_void_v<Xfer>) {
      return fn();
    } else if constexpr (requires { fn(std::move(xf)); }) {
      return fn(std::move(xf));
    } else if constexpr (requires { fn(xf); }) {
      return fn(xf);
    } else {
      return fn();
    }
  }

public:
  ~SequenceBase() = default;

  OXYGEN_MAKE_NON_MOVABLE(SequenceBase)
  OXYGEN_MAKE_NON_COPYABLE(SequenceBase)

  void await_set_executor(Executor* e) noexcept
  {
    first_->awaiter.await_set_executor(e);
    if (InSecondStage()) {
      // The second stage may already exist (a `With` first stage can signal
      // readiness synchronously); keep it and forward the executor.
      if (!second().awaiter.await_ready()) {
        second().awaiter.await_set_executor(e);
      }
    } else {
      second_ = e;
    }
  }

protected:
  struct FirstStage {
    OXYGEN_NO_UNIQUE_ADDRESS First awaitable;
    OXYGEN_NO_UNIQUE_ADDRESS SanitizedAwaiter<First> awaiter;

    explicit FirstStage(First&& aw)
      : awaitable(std::forward<First>(aw))
      , awaiter(std::forward<First>(awaitable))
    {
    }
  };

  using Second = decltype(GetSecond(
    std::declval<ThenFn&>(), std::declval<ReturnType<Xfer>&>()));

  // Explicitly provide a template argument, so immediate awaitables
  // would resolve to Second&& instead of Second.
  // For the same reason, don't use AwaiterType<> here.
  using SecondAwaiter = decltype(GetAwaiter<Second&&>(std::declval<Second>()));

  struct SecondStage {
    OXYGEN_NO_UNIQUE_ADDRESS ReturnType<Xfer> xfer;
    OXYGEN_NO_UNIQUE_ADDRESS Second awaitable;
    OXYGEN_NO_UNIQUE_ADDRESS SanitizedAwaiter<Second&&, SecondAwaiter> awaiter;

    SecondStage(SequenceBase* c, ReturnType<Xfer> xf)
      : xfer(std::forward<ReturnType<Xfer>>(xf))
      , awaitable(GetSecond(c->then_fn_, xfer))
      , awaiter(std::forward<Second>(awaitable))
    {
    }
  };

  SequenceBase(First first, ThenFn then_fn)
    : then_fn_(std::forward<ThenFn>(then_fn))
  {
    first_.emplace(std::forward<First>(first));
  }

  //! Leaves the first stage unconstructed; the derived class must construct
  //! it with `first_.emplace()` before the object is awaited.
  SequenceBase(std::nullptr_t /*no_first*/, ThenFn then_fn)
    : then_fn_(std::forward<ThenFn>(then_fn))
  {
  }

  [[nodiscard]] auto InFirstStage() const noexcept -> bool
  {
    return std::holds_alternative<Executor*>(second_);
  }

  [[nodiscard]] auto InSecondStage() const noexcept -> bool
  {
    return std::holds_alternative<SecondStage>(second_);
  }

  [[nodiscard]] auto HasException() const noexcept -> bool
  {
    return std::holds_alternative<std::exception_ptr>(second_);
  }

  //! The executor set while in the first stage, or `nullptr`.
  [[nodiscard]] auto FirstStageExecutor() const noexcept -> Executor*
  {
    const auto* ex = std::get_if<Executor*>(&second_);
    return ex != nullptr ? *ex : nullptr;
  }

  auto second() noexcept -> SecondStage&
  {
    return *std::get_if<SecondStage>(&second_);
  }

  auto second() const noexcept -> const SecondStage&
  {
    return *std::get_if<SecondStage>(&second_);
  }

  //! Builds the second stage from the transferred value. No stage is active
  //! while `then_fn_` runs, in case it cancels us.
  void SwitchToSecond(ReturnType<Xfer> xf)
  {
    second_.template emplace<std::monostate>();
    second_.template emplace<SecondStage>(
      this, std::forward<ReturnType<Xfer>>(xf));
  }

  //! Destroys the second stage, running its destructors and scope guards.
  void SwitchToNone() const noexcept
  {
    second_.template emplace<std::monostate>();
  }

  void SwitchToException(std::exception_ptr ex) noexcept
  {
    second_.template emplace<std::exception_ptr>(std::move(ex));
  }

  void RethrowIfException() const
  {
    if (const auto* ex = std::get_if<std::exception_ptr>(&second_)) {
      std::rethrow_exception(*ex);
    }
  }

  //! The exception stored by `SwitchToException()`, or `nullptr`.
  [[nodiscard]] auto StoredException() const noexcept -> std::exception_ptr
  {
    const auto* ex = std::get_if<std::exception_ptr>(&second_);
    return ex != nullptr ? *ex : nullptr;
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes) - stage storage
  // shared with the derived awaitables.
  Handle parent_;
  // Declared before `second_`, so the second stage is destroyed first.
  std::optional<FirstStage> first_;
  OXYGEN_NO_UNIQUE_ADDRESS ThenFn then_fn_;
  // NOLINTEND(*-non-private-member-variables-in-classes)

private:
  mutable std::variant<Executor*, // running first stage
    SecondStage, // running second stage,
    std::monostate, // running neither (either constructing
                    // second stage, or it confirmed early
                    // cancellation)
    std::exception_ptr> // a stage threw an exception
    second_;
};

template <Awaitable First, class ThenFn>
class Sequence
  : public SequenceBase<First, ThenFn, AwaitableReturnType<First>> {
  using Base = SequenceBase<First, ThenFn, AwaitableReturnType<First>>;

public:
  Sequence(First first, ThenFn then_fn)
    : Base(std::forward<First>(first), std::forward<ThenFn>(then_fn))
  {
  }

  ~Sequence() = default;

  OXYGEN_MAKE_NON_MOVABLE(Sequence)
  OXYGEN_MAKE_NON_COPYABLE(Sequence)

  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  auto await_early_cancel() noexcept
  {
    cancelling_ = true;
    return this->first_->awaiter.await_early_cancel();
  }

  void await_suspend(const Handle h)
  {
    DLOG_F(5, "   ...sequence {} yielding to...", fmt::ptr(this));
    this->parent_ = h;
    if (this->first_->awaiter.await_ready()) {
      KickOffSecond();
    } else {
      this->resume_fn = +[](CoroutineFrame* frame) -> void {
        auto* self = static_cast<Sequence*>(frame);
        self->KickOffSecond();
      };
      this->first_->awaiter.await_suspend(this->ToHandle()).resume();
    }
  }

  auto await_cancel(Handle h) noexcept -> bool
  {
    DLOG_F(5, "sequence {} ({} stage) cancellation requested", fmt::ptr(this),
      this->InFirstStage() ? "first" : "second");
    cancelling_ = true;
    if (this->InFirstStage()) {
      return this->first_->awaiter.await_cancel(this->ToHandle());
    }
    if (this->InSecondStage()) {
      return this->second().awaiter.await_cancel(h);
    }
    return false; // will carry out cancellation later
  }

  [[nodiscard]] auto await_must_resume() const noexcept -> bool
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
    const bool ret = this->HasException()
      || (this->InSecondStage() && this->second().awaiter.await_must_resume());
    if (!ret && this->InSecondStage()) {
      // Destroy the second stage, which will release any resources
      // it might have held
      this->SwitchToNone();
    }
    return ret;
  }

  auto await_resume() -> decltype(auto)
  {
    ScopeGuard guard([this]() noexcept -> void {
      // Destroy the second stage and the return value of the first stage
      this->SwitchToNone();
    });

    this->RethrowIfException();
    return this->second().awaiter.await_resume();
  }

private:
  void KickOffSecond() noexcept
  {
    if (cancelling_ && !this->first_->awaiter.await_must_resume()) {
      DLOG_F(5,
        "sequence {} (cancelling) first stage completed, "
        "confirming cancellation",
        fmt::ptr(this));
      this->parent_.resume();
      return;
    }

    DLOG_F(5, "sequence {}{} first stage completed, continuing with...",
      fmt::ptr(this), cancelling_ ? " (cancelling)" : "");
    DCHECK_F(this->InFirstStage());
    Executor* ex = this->FirstStageExecutor();

    try {
      this->SwitchToSecond(std::move(this->first_->awaiter).await_resume());
    } catch (...) {
      this->SwitchToException(std::current_exception());
      this->parent_.resume();
      return;
    }

    if (cancelling_) {
      if (this->second().awaiter.await_early_cancel()) {
        this->SwitchToNone();

        this->parent_.resume();
        return;
      }
    }

    if (this->second().awaiter.await_ready()) {
      this->parent_.resume();
    } else {
      this->second().awaiter.await_set_executor(ex);
      this->second().awaiter.await_suspend(this->parent_).resume();
    }
  }

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
      || std::invocable<ThenFn, AwaitableReturnType<First>&&>
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
        T&& value) mutable -> auto {
        return fn(std::forward<T>(value)) | std::move(next);
      });
  }

private:
  ThenFn fn_;
};

} // namespace oxygen::co::detail
