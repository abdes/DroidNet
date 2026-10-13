//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Ported from corral's `with()` combinator (corral/detail/With.h at
// hudson-trading/corral@fcec393), MIT licensed; see OxCo/CORRAL-LICENSE.txt.

#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/CallableSignature.h>
#include <Oxygen/OxCo/Detail/Promise.h>
#include <Oxygen/OxCo/Detail/ProxyFrame.h>
#include <Oxygen/OxCo/Detail/Result.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>
#include <Oxygen/OxCo/Detail/Sequence.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::co::detail {

template <class Callable> auto IntermediateResultImpl()
{
  if constexpr (std::is_invocable_v<Callable, TaskStarted<void>>) {
    // Supports template callables (whose signature cannot be evaluated
    // through `CallableSignature<>`) which happen to take `TaskStarted<void>`.
    return std::type_identity<void> {};
  } else {
    return std::type_identity<typename CallableSignature<
      std::remove_cvref_t<Callable>>::template Arg<0>::ResultType> {};
  }
}

//! The value type `first` passes to `TaskStarted<T>` in `co::With()`.
template <class Callable>
using IntermediateResult =
  typename decltype(IntermediateResultImpl<Callable>())::type;

template <class Fn, class Arg> struct ChainableForWithImpl {
  using ResultType = std::invoke_result_t<Fn, Arg>;
};
template <class Fn> struct ChainableForWithImpl<Fn, void> {
  using ResultType = std::invoke_result_t<Fn>;
};

//! The awaitable produced by `then` in `co::With(first, then)`.
template <class FirstFn, class ThenFn>
using ChainableForWith
  = ChainableForWithImpl<ThenFn, IntermediateResult<FirstFn>>;

//! `With` needs two proxy frames, one per stage; this class provides the
//! second one without making the `ProxyFrame` base ambiguous.
class WithBase : protected ProxyFrame { };

//! Runs `then` inside the lifetime of the supporting awaitable `first`.
/*!
 See `co::With()` for the semantics. The first stage is `first(started)`; the
 second stage is `then(value)`, started once `first` calls `started(value)`.
 Both stages run concurrently; the second stage always finishes, including its
 cancellation, before the first stage is cancelled.
*/
template <class FirstFn, class ThenFn>
class With : private TaskStartedSink<IntermediateResult<FirstFn>>,
             private WithBase,
             public SequenceBase<std::invoke_result_t<FirstFn, TaskStartedTag>,
               ThenFn, IntermediateResult<FirstFn>>,
             public YieldsLikeAwaitTag {
  using Base = SequenceBase<std::invoke_result_t<FirstFn, TaskStartedTag>,
    ThenFn, IntermediateResult<FirstFn>>;
  using Sink = TaskStartedSink<IntermediateResult<FirstFn>>;

  using Ret
    = decltype(std::declval<typename Base::SecondAwaiter>().await_resume());
  using Xfer = ReturnType<IntermediateResult<FirstFn>>;
  using RetValue = ReturnType<Ret>;

  //
  // Per-stage flags, shifted by the stage's `kShift`.
  //

  //! The stage's `await_suspend()` has been called.
  static constexpr uint16_t kStageStarted = 1;
  //! The stage's `await_cancel()` or `await_early_cancel()` has been called.
  //! Not set if the stage completed normally, or was never constructed.
  static constexpr uint16_t kStageCancelling = 2;
  //! The stage invoked its continuation handle.
  static constexpr uint16_t kStageDone = 4;
  //! Cancellation completed synchronously, or `await_must_resume()` returned
  //! false.
  static constexpr uint16_t kStageMustNotResume = 8;

  //
  // Global flags.
  //

  //! `await_cancel()` or `await_early_cancel()` of `With` has been called.
  static constexpr uint16_t kCancelling = 1;

  // Helpers handling both stages uniformly.
  struct FirstStage {
    static constexpr uint16_t kShift = 2;

    static auto Exists(const With& obj) noexcept -> bool
    {
      return obj.first_.has_value();
    }
    static auto Awaiter(With& obj) noexcept -> auto&
    {
      return obj.first_->awaiter;
    }
    static auto Frame(With& obj) noexcept -> CoroutineFrame*
    {
      return static_cast<WithBase*>(&obj);
    }
    static auto FromFrame(CoroutineFrame* frame) noexcept -> With&
    {
      return *static_cast<With*>(static_cast<WithBase*>(frame));
    }
  };

  struct SecondStage {
    static constexpr uint16_t kShift = 6;

    static auto Exists(const With& obj) noexcept -> bool
    {
      return obj.InSecondStage();
    }
    static auto Awaiter(With& obj) noexcept -> auto&
    {
      return obj.second().awaiter;
    }
    static auto Frame(With& obj) noexcept -> CoroutineFrame*
    {
      return static_cast<Base*>(&obj);
    }
    static auto FromFrame(CoroutineFrame* frame) noexcept -> With&
    {
      return *static_cast<With*>(static_cast<Base*>(frame));
    }
  };

  template <class S>
  using OtherStage = std::conditional_t<std::is_same_v<S, FirstStage>,
    SecondStage, FirstStage>;

public:
  With(FirstFn first_fn, ThenFn then_fn)
    : Base(nullptr, std::forward<ThenFn>(then_fn))
    , first_fn_(std::forward<FirstFn>(first_fn))
  {
    // This may immediately call MarkStarted(), which requires a fully
    // initialized `this`.
    this->first_.emplace(
      std::invoke(std::forward<FirstFn>(first_fn_), Sink::MakeTaskStarted()));
    if (this->HasException()) {
      // `then` threw while `first` signalled readiness synchronously; never
      // run the body, and cancel `first` as soon as it is started.
      MarkStageDone<SecondStage>();
      flags_ |= kCancelling;
    }
  }

  ~With() = default;

  OXYGEN_MAKE_NON_COPYABLE(With)
  OXYGEN_MAKE_NON_MOVABLE(With)

  [[nodiscard]] auto await_ready() const noexcept -> bool
  {
    return IsStageDone<FirstStage>() && IsStageDone<SecondStage>();
  }

  auto await_suspend(Handle h) -> Handle
  {
    DLOG_F(5, "   ...with-scope {} yielding to...", fmt::ptr(this));
    this->parent_ = h;

    if (this->InSecondStage()) {
      // `started()` was invoked before the first stage's await_suspend();
      // fire both stages simultaneously.
      BeginStage<FirstStage>().resume();
      return BeginStage<SecondStage>();
    }
    return BeginStage<FirstStage>();
  }

  auto await_cancel(Handle /*h*/) noexcept -> bool
  {
    DLOG_F(5, "with-scope {} ({} stage) cancellation requested", fmt::ptr(this),
      this->InSecondStage() ? "second" : "first");
    return DoCancel();
  }

  auto await_early_cancel() noexcept -> bool { return DoCancel(); }

  [[nodiscard]] auto await_must_resume() const noexcept -> bool
  {
    DCHECK_F(!this->InSecondStage());
    return this->HasException() || MustStageResume<FirstStage>()
      || result_.has_value();
  }

  auto await_resume() && -> Ret
  {
    this->RethrowIfException();

    if (MustStageResume<FirstStage>()) {
      // The first stage completed on its own. It may only do so by throwing,
      // which this call propagates.
      static_cast<void>(std::move(this->first_->awaiter).await_resume());
      CHECK_F(false, "Background awaitable completed without an error");
    }

    if constexpr (!std::is_void_v<Ret>) {
      return Storage<RetValue>::Unwrap(std::move(*result_));
    }
  }

private:
  template <class S> [[nodiscard]] auto Flag(const uint16_t flag) const noexcept
  {
    return (flags_ & (flag << S::kShift)) != 0;
  }
  template <class S> void SetFlag(const uint16_t flag) noexcept
  {
    flags_ = static_cast<uint16_t>(flags_ | (flag << S::kShift));
  }

  template <class S> [[nodiscard]] auto IsStageStarted() const noexcept -> bool
  {
    return Flag<S>(kStageStarted);
  }
  template <class S>
  [[nodiscard]] auto IsStageCancelling() const noexcept -> bool
  {
    return Flag<S>(kStageCancelling);
  }
  template <class S> [[nodiscard]] auto IsStageDone() const noexcept -> bool
  {
    return Flag<S>(kStageDone);
  }
  template <class S> [[nodiscard]] auto MustStageResume() const noexcept -> bool
  {
    return !Flag<S>(kStageMustNotResume);
  }
  template <class S> [[nodiscard]] auto IsStageRunning() const noexcept -> bool
  {
    return IsStageStarted<S>() && !IsStageDone<S>();
  }
  template <class S> void MarkStageStarted() noexcept
  {
    SetFlag<S>(kStageStarted);
  }
  template <class S> void MarkStageDone() noexcept { SetFlag<S>(kStageDone); }
  template <class S> void MarkStageCancelling() noexcept
  {
    SetFlag<S>(kStageCancelling);
  }
  template <class S> void MarkStageMustNotResume() noexcept
  {
    SetFlag<S>(kStageMustNotResume);
  }

  template <class S> [[nodiscard]] auto BeginStage() -> Handle
  {
    if (IsStageDone<S>()) {
      return NoOpHandle();
    }
    auto& awaiter = S::Awaiter(*this);

    if (((flags_ & kCancelling) != 0) && !IsStageCancelling<S>()) {
      MarkStageCancelling<S>();
      if (awaiter.await_early_cancel()) {
        MarkStageMustNotResume<S>();
        return OnStageDone<S>();
      }
    }

    if (awaiter.await_ready()) {
      return OnStageDone<S>();
    }

    MarkStageStarted<S>();
    CoroutineFrame* frame = S::Frame(*this);
    frame->resume_fn = +[](CoroutineFrame* fr) -> void {
      S::FromFrame(fr).template OnStageDone<S>().resume();
    };

    try {
      return awaiter.await_suspend(frame->ToHandle());
    } catch (...) {
      this->SwitchToException(std::current_exception());
      return OnStageDone<S>();
    }
  }

  template <class S> auto CancelStage() noexcept -> bool
  {
    if (IsStageCancelling<S>()) {
      return false; // cancellation already in progress
    }
    if (!S::Exists(*this) || IsStageDone<S>()) {
      return true; // not yet created, or already finished
    }

    MarkStageCancelling<S>();
    bool cancelled = false;
    if (IsStageStarted<S>()) {
      cancelled = S::Awaiter(*this).await_cancel(S::Frame(*this)->ToHandle());
    } else {
      cancelled = S::Awaiter(*this).await_early_cancel();
    }

    if (cancelled) {
      MarkStageMustNotResume<S>();
      MarkStageDone<S>();
    }
    return cancelled;
  }

  template <class S> [[nodiscard]] auto OnStageDone() noexcept -> Handle
  {
    DLOG_F(5, "with-scope {} {} stage done", fmt::ptr(this),
      std::is_same_v<S, FirstStage> ? "first" : "second");

    MarkStageDone<S>();
    if (MustStageResume<S>() && IsStageCancelling<S>() && S::Exists(*this)
      && !S::Awaiter(*this).await_must_resume()) {
      // await_must_resume() cannot be called multiple times, so memorize it.
      MarkStageMustNotResume<S>();
    }

    if constexpr (std::is_same_v<S, SecondStage>) {
      if (this->InSecondStage() && MustStageResume<S>()) {
        // await_resume() needs the second stage's result later, but the stage
        // is destroyed right away (see below), so stash the result.
        try {
          result_.emplace(
            Storage<RetValue>::Wrap(this->second().awaiter.await_resume()));
        } catch (...) {
          this->SwitchToException(std::current_exception());
        }
      }
      if (this->InSecondStage()) {
        // Destroy the second stage (running its destructors and scope guards)
        // before the first stage's cancellation starts.
        this->SwitchToNone();
      }
    }

    using O = OtherStage<S>;
    if (IsStageDone<O>() || CancelStage<O>()) {
      DLOG_F(
        5, "with-scope {} all stages done, resuming parent", fmt::ptr(this));
      return this->parent_;
    }
    return NoOpHandle();
  }

  auto DoCancel() noexcept -> bool
  {
    flags_ |= kCancelling;
    // Decide whether a stage runs from its own flags, never from the stage
    // storage: once the second stage completed and was destroyed, the storage
    // no longer tells whether the first stage still runs.
    if (IsStageRunning<SecondStage>()) {
      // Short-circuiting is essential: the first stage's cancellation must
      // not start until the second stage completes.
      return CancelStage<SecondStage>() && CancelStage<FirstStage>();
    }
    return CancelStage<FirstStage>() && !IsStageRunning<SecondStage>();
  }

  void MarkStarted(Xfer value) noexcept override
  {
    DLOG_F(5, "with-scope {} first stage ready", fmt::ptr(this));
    DCHECK_F(!IsStageDone<FirstStage>(),
      "MarkStarted() called after the first stage is done");

    if (IsStageCancelling<FirstStage>()) {
      // The first stage still runs, but has a pending cancellation and can
      // stop at any moment; it is not safe to start the second stage.
      MarkStageDone<SecondStage>();
      return;
    }

    // This can run before await_suspend(), with no executor yet.
    Executor* executor = this->FirstStageExecutor();

    try {
      this->SwitchToSecond(std::forward<Xfer>(value));
    } catch (...) {
      this->SwitchToException(std::current_exception());
      // Still constructing: the constructor handles it once `first_` exists.
      if (this->first_.has_value() && CancelStage<FirstStage>()) {
        this->parent_.resume();
      }
      return;
    }

    if (((flags_ & kCancelling) != 0) && CancelStage<SecondStage>()) {
      OnStageDone<SecondStage>().resume();
    } else if (executor != nullptr) {
      this->second().awaiter.await_set_executor(executor);
      BeginStage<SecondStage>().resume();
    } else {
      // The second stage is kicked off from await_suspend().
    }
  }

  FirstFn first_fn_;
  std::optional<typename Storage<RetValue>::Type> result_;
  uint16_t flags_ = 0;
};

//! The awaitable produced by `OXCO_WITH`: constructs the non-movable `With`
//! in place when awaited, and is tagged so that `co_yield` awaits it.
template <class FirstFn, class ThenFn>
class WithYieldable
  : public AwaiterMaker<With<FirstFn, ThenFn>, FirstFn, ThenFn>,
    public YieldsLikeAwaitTag {
public:
  using AwaiterMaker<With<FirstFn, ThenFn>, FirstFn, ThenFn>::AwaiterMaker;
};

//! Builds a `With` from `OXCO_WITH(first, ...)`; `operator%` binds the body.
template <class FirstFn> class WithBuilder {
public:
  explicit WithBuilder(FirstFn first_fn)
    : first_fn_(std::move(first_fn))
  {
  }

  template <class ThenFn> auto operator%(ThenFn then_fn) &&
  {
    return WithYieldable<FirstFn, ThenFn>(
      std::move(first_fn_), std::move(then_fn));
  }

private:
  FirstFn first_fn_;
};

} // namespace oxygen::co::detail
