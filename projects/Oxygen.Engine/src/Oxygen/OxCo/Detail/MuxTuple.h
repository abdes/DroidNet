//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Detail/MuxBase.h>
#include <Oxygen/OxCo/Detail/Optional.h>
#include <Oxygen/OxCo/Detail/Result.h>

#include <tuple>

namespace oxygen::co::detail {

//! Multiplexer designed to manage multiple awaitables in scenarios such as
//! AnyOf(), MostOf(), and AllOf().
template <class Self, class... Awaitables>
class MuxTuple : public MuxBase<Self> {
public:
  //! Constructs a mux with the given awaitables.
  /*!
   Accepts multiple awaitables and wraps each one using `MuxHelper`, storing
   them in a `std::tuple`. Each `MuxHelper` manages the state and execution of
   its corresponding awaitable.
   */
  explicit MuxTuple(Awaitables&&... awaitables)
    : awaitables_(std::forward<Awaitables>(awaitables)...)
  {
  }

  static constexpr auto IsAbortable() -> bool
  {
    // See comments in `MuxBase::await_cancel()` regarding why we only can
    // propagate `Abortable` if the mux completes when its first awaitable does.
    return Self::MinReady() == 1 && (Abortable<AwaiterType<Awaitables>> && ...);
  }

  static constexpr auto IsSkippable() -> bool
  {
    return (Skippable<AwaiterType<Awaitables>> && ...);
  }

  static constexpr auto Size() noexcept -> size_t
  {
    return sizeof...(Awaitables);
  }

  void await_set_executor(Executor* ex) noexcept
  {
    auto impl
      = [ex](auto&... awaitables) { (awaitables.SetExecutor(ex), ...); };
    std::apply(impl, awaitables_);
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool
  {
    auto impl = [](const auto&... aws) {
      const size_t n_ready = (static_cast<size_t>(aws.IsReady()) + ...);
      const size_t n_skip_kickoff
        = (static_cast<size_t>(aws.IsReady() || aws.IsSkippable()) + ...);
      return n_ready >= Self::MinReady()
        && n_skip_kickoff == sizeof...(Awaitables);
    };
    return std::apply(impl, awaitables_);
  }

  auto await_suspend(Handle h) -> bool
  {
    const bool ret = this->DoSuspend(h);
    auto impl = [this](auto&... awaitables) {
      (awaitables.Bind(*static_cast<Self*>(this)), ...);
      (awaitables.Suspend(), ...);
    };
    std::apply(impl, awaitables_);
    return ret;
  }

  auto
  await_resume() && -> std::tuple<Optional<AwaitableReturnType<Awaitables>>...>
  {
    HandleResumeWithoutSuspend();
    this->ReRaise();
    auto impl = [](auto&&... awaitables) {
      return std::make_tuple(std::move(awaitables).AsOptional()...);
    };
    return std::apply(impl, std::move(awaitables_));
  }

  // This is called in two situations:
  // - enough awaitables have completed, and we want to cancel the rest, or
  // - we get a cancellation from the outside (await_cancel).
  //
  // Returns true if every awaitable is now cancelled (i.e., we definitely
  // have no results to report to our parent), which is only relevant to
  // `await_cancel()`.
  auto InternalCancel() -> bool
  {
    auto impl = [](auto&... awaitables) {
      // Don't short-circuit; we should try to cancel every remaining
      // awaitable, even if some have completed already.
      return (... & static_cast<int>(awaitables.Cancel()));
    };
    return std::apply(impl, awaitables_);
  }

  auto await_must_resume() const noexcept
  {
    auto impl = [](const auto&... awaitables) {
      // Don't short-circuit; we should check mustResume of every
      // awaitable, since it can have side effects.
      return (... | static_cast<int>(awaitables.MustResume()));
    };
    bool any_must_resume = std::apply(impl, awaitables_);

    // It is not generally true that CancelAlwaysSucceeds for a mux even if
    // it is true for each of the constituents. If one awaitable completes,
    // it might take some time for the others to finish cancelling, and a
    // cancellation of the overall mux during this time would fail (because
    // the mux already has a value to report to its parent).
    //
    // If all cancels for this mux complete synchronously, though, we know
    // we'll never have a must-resume situation, because the length of the
    // time interval described in the previous paragraph is zero.
    if constexpr (Skippable<Self> && Abortable<Self>) {
      DCHECK_F(!any_must_resume);
      return std::false_type {};
    } else {
      return any_must_resume;
    }
  }

protected:
  [[nodiscard]] auto GetAwaiters() -> auto& { return awaitables_; }
  [[nodiscard]] auto GetAwaiters() const -> const auto& { return awaitables_; }

  void HandleResumeWithoutSuspend()
  {
    // We skipped await_suspend because all awaitables were ready or
    // sync-early-cancellable. (All the MuxHelpers have `Bind()` called
    // at the same time; we check the first one for convenience only.)
    if (!std::get<0>(awaitables_).IsBound()) {
      [[maybe_unused]] auto _ = this->DoSuspend(std::noop_coroutine());
      auto extract_results = [this](auto&&... awaitables) {
        (awaitables.Bind(*static_cast<Self*>(this)), ...);
        (awaitables.ReportImmediateResult(), ...);
      };
      std::apply(extract_results, awaitables_);
    }
  }

private:
  // CRTP: Mixin constructor must be private to prevent direct instantiation,
  // derived class must be a friend.
  friend Self;

  std::tuple<MuxHelper<Self, Awaitables>...> awaitables_;
};

//! Specialization of `MuxTuple` for zero awaitables.
template <class Self> class MuxTuple<Self> : public MuxBase<Self> {
public:
  static constexpr auto IsAbortable() -> bool { return true; }
  static constexpr auto IsSkippable() -> bool { return true; }
  static constexpr auto Size() noexcept -> size_t { return 0; }
  // ReSharper disable CppMemberFunctionMayBeStatic
  auto InternalCancel() noexcept -> bool { return true; }
  [[nodiscard]] auto await_ready() const noexcept -> bool { return true; }
  auto await_suspend([[maybe_unused]] Handle h) -> bool { return false; }
  auto await_resume() && -> std::tuple<> { return {}; }
  // ReSharper restore CppMemberFunctionMayBeStatic

protected:
  // ReSharper disable CppMemberFunctionMayBeStatic
  [[nodiscard]] auto GetAwaiters() const -> std::tuple<> { return {}; }
  void HandleResumeWithoutSuspend() { }
  // ReSharper restore CppMemberFunctionMayBeStatic
};

template <class... Awaitables>
class AnyOfMux : public MuxTuple<AnyOfMux<Awaitables...>, Awaitables...> {
public:
  using AnyOfMux::MuxTuple::MuxTuple;
  static constexpr auto MinReady() noexcept { return static_cast<size_t>(1); }
};

template <class... Awaitables>
class MostOfMux : public MuxTuple<MostOfMux<Awaitables...>, Awaitables...> {
public:
  using MostOfMux::MuxTuple::MuxTuple;
  static constexpr auto MinReady() noexcept { return sizeof...(Awaitables); }
};

template <class... Awaitables>
class AllOfMux : public MuxTuple<AllOfMux<Awaitables...>, Awaitables...> {
public:
  using AllOfMux::MuxTuple::MuxTuple;

  static constexpr auto MinReady() noexcept { return sizeof...(Awaitables); }

  auto await_must_resume() const noexcept
  {
    // We only require the parent to be resumed if *all* awaitables
    // have a value to report, so we'd be able to construct a tuple of
    // results (or if any awaitable failed, so we'd re-raise and not
    // bother with return value construction).
    auto impl = [](const auto&... awaitables) {
      if constexpr (sizeof...(awaitables) == 0) {
        return false;
      } else {
        return (... & static_cast<int>(awaitables.MustResume()));
      }
    };
    bool ret = this->HasException() || std::apply(impl, this->GetAwaiters());

    // See note in MuxTuple::await_must_resume(). Note there is no way
    // for AllOf to satisfy Abortable in nontrivial cases (only with zero
    // or one awaitable).
    if constexpr (Skippable<AllOfMux> && Abortable<AllOfMux>) {
      DCHECK_F(!ret);
      return std::false_type {};
    } else {
      return ret;
    }
  }

  auto await_resume() && -> std::tuple<AwaitableReturnType<Awaitables>...>
  {
    this->HandleResumeWithoutSuspend();
    this->ReRaise();
    auto impl = [](auto&&... awaitables) {
      return std::make_tuple(std::move(awaitables).Result()...);
    };
    return std::apply(impl, std::move(this->GetAwaiters()));
  }
};

} // namespace oxygen::co::detail
