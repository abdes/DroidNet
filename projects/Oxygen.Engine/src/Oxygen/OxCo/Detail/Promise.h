//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/NoInline.h"
#include "Oxygen/Base/ReturnAddress.h"
#include "Oxygen/OxCo/Detail/AwaitFn.h"
#include "Oxygen/OxCo/Detail/CoRoutineFrame.h"
#include "Oxygen/OxCo/Detail/GetAwaiter.h"
#include "Oxygen/OxCo/Detail/IntrusiveList.h"
#include "Oxygen/OxCo/Detail/SanitizedAwaiter.h"
#include "Oxygen/OxCo/Detail/TaskFrame.h"
#include "Oxygen/OxCo/Detail/TaskParent.h"
#include "Oxygen/OxCo/Executor.h"

namespace oxygen::co {

template <class T>
class Co;

namespace detail {
    struct RethrowCurrentException;

    //! Base class for the `Promise` object of a coroutine, contains the parts
    //! that are independent of the return type.
    /*!
     When a coroutine is suspended, the compiler will automatically create a
     promise object for it. Its type is exactly `promise_type` which must be
     defined within the return type of the coroutine. The promise object manages
     the state of coroutine execution, and is manipulated internally within the
     coroutine.

     When a coroutine is started, the `get_return_object` method of the promise
     object is called, and its return value, a `Task` object that will hold the
     result of the coroutine execution, is returned to the caller when the
     coroutine first suspends.
     */
    class BasePromise : /*private*/ TaskFrame, public IntrusiveListItem<BasePromise> {

        //! A type erased control block for cancellable tasks that captures how
        //! to cancel the `Awaiter` for the coroutine associated with this
        //! `Promise`.
        /*!
         The control block consists of two non-null pointers. The first
         (`object_`) refers to the `Awaitable` object in a type-erased way. The
         second (`functions_`) refers to a set of functions that can be used to
         manipulate the `Awaitable` object during the process of cancellation.

         When not used as a cancellation control block, the 'first' pointer slot
         is used to store the current state of the coroutine execution, and the
         'second' pointer slot is used to store the current state of the
         cancellation.

         The `object_` pointer starts at offset `0` in the struct, and the
         `functions_` pointer is aligned at a word size (4 bytes on 32 bits, and
         8 bytes on 64 bits). This allows to distinguish between the two states,
         and we can reasonably assume that the state values can be chosen to not
         correspond to valid addresses.

         \see Execution
         \see Cancellation
         */
        class CancellationControlBlock {
        public:
            template <Awaitable Aw>
            explicit CancellationControlBlock(Aw& object)
                : CancellationControlBlock(&object, Control<Aw>())
            {
            }

            [[nodiscard]] auto Cancel(const Handle h) const noexcept
            {
                return functions_->Cancel(object_, h);
            }
            [[nodiscard]] auto MustResume() const noexcept
            {
                return functions_->MustResume(object_);
            }

        private:
            //! Type-erased cancellation control functions, enabling the
            //! `CancellationControlBlock` to deal with `Awaitables` simply as
            //! cancellable objects.
            class IControlFunctions {
            public:
                [[nodiscard]] virtual auto Cancel(void* aw, Handle h) const noexcept -> bool = 0;
                [[nodiscard]] virtual auto MustResume(const void* aw) const noexcept -> bool = 0;

            protected:
                IControlFunctions() = default;
                ~IControlFunctions() = default;
                OXYGEN_DEFAULT_COPYABLE(IControlFunctions)
                OXYGEN_DEFAULT_MOVABLE(IControlFunctions)
            };

            //! Type-specific cancellation control functions.
            template <class T>
            class ControlFunctions final : public IControlFunctions {
            public:
                ControlFunctions() = default;
                ~ControlFunctions() = default;
                OXYGEN_DEFAULT_COPYABLE(ControlFunctions)
                OXYGEN_DEFAULT_MOVABLE(ControlFunctions)

                auto Cancel(void* aw, Handle h) const noexcept -> bool override
                {
                    return AwaitCancel(*static_cast<T*>(aw), h);
                }
                auto MustResume(const void* aw) const noexcept -> bool override
                {
                    return AwaitMustResume(*static_cast<const T*>(aw));
                }
            };

            template <class T>
            static auto Control()
            {
                static const ControlFunctions<T> ret;
                return &ret;
            }

            //! The type-erased constructor. Takes an `Awaitable` object and its
            //! set of cancellation control functions.
            CancellationControlBlock(void* object, const IControlFunctions* functions)
                : object_(object)
                , functions_(functions)
            {
            }

            //! The type-erased pointer to the `Awaitable` object.
            void* object_ { nullptr };
            //! The type-erased pointer to the cancellation control functions.
            const IControlFunctions* functions_ { nullptr };
        };

        static_assert(std::is_trivially_copyable_v<CancellationControlBlock>);
        static_assert(std::is_trivially_destructible_v<CancellationControlBlock>);

        //! The execution state of the coroutine associated with this `Promise`.
        /*!
         \note This enum lives in a union with the cancellation control block
               (`ccb_`). It occupies the first pointer slot, corresponding to
               the awaitable `object_` pointer, and as such, its value must be
               distinguishable from a valid.
         */
        enum class Execution : size_t { // NOLINT(performance-enum-size)
            kReady = 0, //!< Used for tasks scheduled for execution (i.e. whose proxy handled was `Resume()`ed).
            kRunning = 1, //!< Used for tasks currently being executed (i.e. whose real handle was `Resume()`ed).
            kStub = 2 //!< Used for promises not associated with a coroutine, such as `Just` or `NoOp`.

            // Anything else is a coroutine suspended on an `Awaitable` and its
            // `CancellationControlBlock` is populated accordingly.
        };

        //! The cancellation state of the coroutine associated with this `Promise`.
        /*!
         \note This enum lives in a union with the cancellation control block
               (`ccb_`). It occupies the second pointer slot, corresponding to
               the awaitable control `functions_` pointer, and as such, its
               value must be distinguishable a valid pointer.
         */
        enum class Cancellation : size_t { // NOLINT(performance-enum-size)
            kNone = 0,
            //! Used for `kRunning` or `kReady` tasks that have been requested
            //! to be cancelled. Such a task will get cancelled as soon as
            //! possible.
            kRequested = 1
        };

        //! The `CancellationControlBlock` and the coroutine state, living
        //! together in a union.
        union {
            CancellationControlBlock ccb;
            struct {
                Execution execution;
                Cancellation cancellation;
            } state {
                .execution = Execution::kReady,
                .cancellation = Cancellation::kNone,
            };
        } info_ {};

        [[nodiscard]] auto ExecutionState() const noexcept { return info_.state.execution; }
        [[nodiscard]] auto CancellationState() const noexcept { return info_.state.cancellation; }

        void ExecutionState(const Execution state) noexcept { info_.state.execution = state; }
        void CancellationState(const Cancellation state) noexcept { info_.state.cancellation = state; }

        [[nodiscard]] auto HasAwaiter() const noexcept { return ExecutionState() > Execution::kStub; }
        [[nodiscard]] auto HasCoroutine() const noexcept { return ExecutionState() != Execution::kStub; }

        [[nodiscard]] auto AwCancel(const Handle h) const noexcept -> bool { return info_.ccb.Cancel(h); }
        [[nodiscard]] auto AwMustResume() const noexcept -> bool { return info_.ccb.MustResume(); }

        template <Awaitable Aw>
        void ResetControlBlock(Aw& aw) { info_.ccb = CancellationControlBlock(aw); }

    public:
        ~BasePromise() override
        {
            DLOG_F(1, "pr {} destroyed", fmt::ptr(this));
        }

        void SetExecutor(Executor* ex) { executor_ = ex; }

        //! Requests the cancellation of the running task.
        /*!
         If the promise's current awaitee (if any) supports cancellation,
         proxies the request to the awaitee through the cancellation control
         block; otherwise marks the task as pending cancellation, and any
         further `co_await` on a cancellable awaiter would result in immediate
         cancellation.

         In either case, if the awaitee is in fact cancelled (as opposed to
         completing its operation despite the cancellation request), the
         awaiting task will also terminate by cancellation, and so on up the
         stack.
        */
        void Cancel()
        {
            DLOG_F(1, "pr {} cancellation requested", fmt::ptr(this));

            if (!HasAwaiter()) {
                // Mark pending cancellation; coroutine will be cancelled
                // at its next suspension point (for running coroutines) or when
                // executed by executor (for ready coroutines). This is a no-op
                // if cancel() was already called.
                CancellationState(Cancellation::kRequested);
            } else {
                // Coroutine currently suspended, so intercept the flow at
                // its resume point, and forward cancellation request to the
                // `Awaitable`.
                OnResume<&BasePromise::DoResumeAfterCancel>();
                if (AwCancel(ProxyHandle())) {
                    PropagateCancel();
                }
            }
        }

        //! Destroys the promise and any locals within the coroutine frame. Only
        //! safe to call on not-yet-started tasks or those already completed
        //! (i.e., whose parent has resumed).
        void Destroy()
        {
            if (HasCoroutine()) { // NOLINT(bugprone-branch-clone)
                RealHandle().destroy();
            } else {
                // Call the `TaskFrame::destroy_fn` filled in by `MakeStub()`.
                // This is the only place where that's actually a function
                // pointer; normally we use it as a parent-task link.
                ProxyHandle().destroy();
            }
        }

        auto CheckImmediateResult(BaseTaskParent* parent) noexcept
        {
            if (!HasCoroutine()) {
                // If we have a value to provide immediately, then provide
                // it without a trip through the executor
                parent_ = parent;
                // Invoke callback stashed by makeStub()
                resume_fn(this);
                // Make sure it's only called once
                resume_fn = +[](CoroutineFrame*) { };
                return true;
            }
            return false;
        }

    protected:
        BasePromise()
        {
            DLOG_F(1, "pr {} created", fmt::ptr(this));
        }

        OXYGEN_MAKE_NON_COPYABLE(BasePromise)
        OXYGEN_MAKE_NON_MOVEABLE(BasePromise)

        /// Replaces the parent of an already started task.
        void ReParent(BaseTaskParent* parent, const Handle caller)
        {
            parent_ = parent;
            LinkTo(caller);
        }

        /// Returns a handle which would schedule task startup if resume()d or
        /// returned from an await_suspend() elsewhere.
        /// `parent` is an entity which arranged the execution and which will get
        /// notified (through parent->continuation().resume()) upon coroutine
        /// completion.
        auto Start(BaseTaskParent* parent, const Handle caller) -> Handle
        {
            if (CheckImmediateResult(parent)) {
                return parent->Continuation(this);
            }
            ReParent(parent, caller);
            DLOG_F(1, "pr {} started", fmt::ptr(this));
            OnResume<&BasePromise::DoResume>();
            return ProxyHandle();
        }

        // ReSharper disable once CppHiddenFunction
        [[nodiscard]] auto Parent() const noexcept { return parent_; }

        //! Cause this promise to not resume a coroutine when it is started.
        //! Instead, it will invoke the given callback and then resume its
        //! parent.
        /*!
         This can be used to create promises that are not associated with a
         coroutine. Must be called before `Start()`.

         \see Just()
         \see NoOp()
        */
        template <class Derived, void (Derived::*OnStart)()>
        void MakeStub(const bool delete_this_on_destroy)
        {
            DCHECK_F(ExecutionState() == Execution::kReady && parent_ == nullptr);
            ExecutionState(Execution::kStub);
            ProgramCounter(0);

            // Since stub promises never use their inline CoroutineFrame,
            // we can reuse them to store callbacks for start and destroy
            resume_fn = +[](CoroutineFrame* self) {
                (static_cast<Derived*>(self)->*OnStart)();
            };
            if (delete_this_on_destroy) {
                destroy_fn = +[](CoroutineFrame* self) {
                    // NOLINTNEXTLINE(*-owning-memory)
                    delete static_cast<Derived*>(self);
                };
            } else {
                destroy_fn = +[](CoroutineFrame* /*self*/) { };
            }
        }

    private:
        //! Returns the real handle which, when `resume()`d, will immediately
        //! execute the next step of the coroutine.
        [[nodiscard]] auto RealHandle() noexcept -> CoroutineHandle<BasePromise>
        {
            // NB: this is technically UB, as C++ does not allow up- or down-
            // casting `coroutine_handle<>`s. However, on any reasonable
            // implementation coroutine_handle<>::from_promise just shifts
            // promise address by a fixed, implementation-defined offset; so
            // provided BasePromise is the first base of the most derived
            // promise type, this should work fine.
            return CoroutineHandle<BasePromise>::from_promise(*this);
        }

        //! Returns a proxy handle which, when `resume()`d, will schedule the
        //! next step of the task to run through its executor (or cancel it, if
        //! the cancellation is pending).
        [[nodiscard]] auto ProxyHandle() noexcept -> Handle
        {
            return ToHandle();
        }

        //! Installs a trampoline function to replace the regular resume
        //! function of the promise.
        template <void (BasePromise::*TrampolineFn)()>
        void OnResume()
        {
            resume_fn = +[](CoroutineFrame* frame) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
                auto* const promise = static_cast<BasePromise*>(frame);
                (promise->*TrampolineFn)();
            };
        }

        //! A resume trampoline that does nothing.
        void DoNothing()
        {
            DLOG_F(1, "pr {} already scheduled, skipping", fmt::ptr(this));
        }

        //! A resume trampoline that schedules the task for execution, and what
        //! that happens, the actual promise resume function is called.
        void DoResume()
        {
            DLOG_F(1, "pr {} scheduled", fmt::ptr(this));
            ExecutionState(Execution::kReady);

            // Prevent further doResume()s from scheduling the task again
            OnResume<&BasePromise::DoNothing>();

            executor_->RunSoon(
                +[](void* arg) noexcept {
                    const auto h = CoroutineHandle<BasePromise>::from_address(arg);
                    BasePromise& self = h.promise();
                    DLOG_F(1, "pr {} resumed", fmt::ptr(&self));
                    self.ExecutionState(Execution::kRunning);
                    h.resume();
                },
                RealHandle().address());
        }

        //! A resume trampoline that helps when a coroutine for which
        //! cancellation has been requested but current awaited task completed
        //! normally. In that case, the cancellation will be attempted again on
        //! the next `co_await` suspension.
        void DoResumeAfterCancel()
        {
            if (HasAwaiter() && AwMustResume()) {
                // This task completed normally, so don't propagate the
                // cancellation. Attempt it again on the next co_await.
                CancellationState(Cancellation::kRequested);
                DoResume();
            } else {
                PropagateCancel();
            }
        }

        //! Actually propagate a cancellation; called if `await_cancel()`
        //! returns true or if we're resumed after cancelling and
        //! `await_must_resume()` returns `false`.
        void PropagateCancel()
        {
            DLOG_F(1, "pr {} cancelled", fmt::ptr(this));
            BaseTaskParent* parent = std::exchange(parent_, nullptr);
            parent->Cancelled();
            parent->Continuation(this).resume();
        }

        //! @{
        //! Hooks into suspend points of the coroutine, to get a chance to
        //! access the `Awaiter` and install the cancellation control logic.

        //! Hooks onto `Aw::await_suspend()` and keeps track of the awaiter so
        //! cancellation can be arranged if necessary.
        /*!
         This function is called when *this* task blocks on an awaiter.
        */
        template <Awaiter Awaiter>
        auto HookAwaitSuspend(Awaiter& awaiter) -> Handle
        {
            const bool cancel_requested = CancellationState() == Cancellation::kRequested;
            DLOG_F(1, "pr {} suspended {}", fmt::ptr(this),
                cancel_requested ? "(with pending cancellation) on..." : "");
            ResetControlBlock(awaiter); // this resets cancelState_

            if (cancel_requested) {
                if (AwaitEarlyCancel(awaiter)) {
                    DLOG_F(1, "    ... early-cancelled awaiter (skipped)");
                    PropagateCancel();
                    return std::noop_coroutine();
                }
                OnResume<&BasePromise::DoResumeAfterCancel>();
                if (awaiter.await_ready()) {
                    DLOG_F(1, "    ... already-ready awaiter");
                    return ProxyHandle();
                }
            } else {
                OnResume<&BasePromise::DoResume>();
            }
            awaiter.await_set_executor(executor_);

            try {
                return detail::AwaitSuspend(awaiter, ProxyHandle());
            } catch (...) {
                DLOG_F(1, "pr {}: exception thrown from await_suspend", fmt::ptr(this));
                ExecutionState(Execution::kRunning);
                if (cancel_requested) {
                    // ReSharper disable once CppDFAUnreachableCode
                    CancellationState(Cancellation::kRequested);
                }
                // ReSharper disable once CppDFAUnreachableCode
                throw;
            }
        }

        //! A proxy which allows Promise to have control over its own suspension.
        template <Awaitable Awaitable>
        class AwaitProxy {
            // Use decltype instead of AwaiterType in order to reference
            // (rather than moving) an incoming Awaitable&& that is
            // ImmediateAwaitable; even if it's a temporary, it will live for
            // the entire co_await expression including suspension.
            using AwaiterType = decltype(GetAwaiter(std::declval<Awaitable>()));

        public:
            // The reference will be valid for the entire `co_await` expression.
            // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
            AwaitProxy(BasePromise* promise, Awaitable&& awaitable) noexcept
                : awaiter_(std::forward<Awaitable>(awaitable))
                , promise_(promise)
            {
            }

            [[nodiscard]] auto await_ready() const noexcept
            {
                if (promise_->CancellationState() == Cancellation::kRequested) {
                    // If the awaiting task has pending cancellation, we want
                    // to execute the more involved logic in HookAwaitSuspend().
                    return false;
                }
                return awaiter_.await_ready();
            }

            OXYGEN_NOINLINE auto await_suspend(Handle /*unused*/)
            {
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                promise_->ProgramCounter(reinterpret_cast<uintptr_t>(OXYGEN_RETURN_ADDRESS()));
                return promise_->HookAwaitSuspend(awaiter_);
            }

            auto await_resume() -> decltype(auto)
            {
                return awaiter_.await_resume();
            }

        private:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
            [[no_unique_address]] SanitizedAwaiter<Awaitable, AwaiterType> awaiter_;
            BasePromise* promise_;
        };

        //! Hook into the final suspend point of the coroutine, to control the
        //! continuation logic.
        auto HookFinalSuspend() -> Handle
        {
            DLOG_F(1, "pr {} finished", fmt::ptr(this));
            BaseTaskParent* parent = std::exchange(parent_, nullptr);
            DCHECK_NOTNULL_F(parent);
            return parent->Continuation(this);
        }

        //! A proxy which allows a promise to control its own finalization.
        class FinalSuspendProxy : public std::suspend_always {
        public:
            template <class Promise>
            // ReSharper disable once CppMemberFunctionMayBeStatic
            auto await_suspend(CoroutineHandle<Promise> h) noexcept -> Handle
            {
                return h.promise().HookFinalSuspend();
            }
        };

        //! @}

        Executor* executor_ = nullptr;
        BaseTaskParent* parent_ = nullptr;

    public:
        OXYGEN_NOINLINE auto initial_suspend() noexcept
        {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            ProgramCounter(reinterpret_cast<uintptr_t>(OXYGEN_RETURN_ADDRESS()));
            return std::suspend_always {};
        }

        // ReSharper disable once CppMemberFunctionMayBeStatic
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto final_suspend() noexcept { return FinalSuspendProxy(); }

        //! An implementation of `await_transform()` that stashes the awaitable
        //! along with its type in a proxy object for later use during
        //! cancellation
        /*!
         When an awaitable is suspended, we have no knowledge of the awaitable
         or its type, which are needed for cancellation. This function takes
         advantage of the fact that all the awaitable methods will be invoked on
         whatever object returned from `await_transform()`.

         The `AwaitProxy` interceptor implements the `await_suspend()` method to
         delegate to `HookAwaitSuspend()`, which in turn, stashes the awaitable
         and its type in the promise's cancellation control block. The
         cancellation block has the type-erased pointer to the awaitable, and a
         set of type-erased functions that can be used to manipulate the
         awaitable during the process of cancellation.
        */
        template <class Awaitable>
        auto await_transform(Awaitable&& awaitable) noexcept
        {
            static_assert(!std::is_same_v<std::decay_t<Awaitable>, FinalSuspendProxy>);

            // Note: intentionally not constraining Aw here to get a nicer
            // compilation error (constraint will be checked in
            // `GetAwaiter()`).
            return AwaitProxy<Awaitable>(this, std::forward<Awaitable>(awaitable));
        }

        friend RethrowCurrentException;
    };

    template <class T>
    class Promise;

    template <class T>
    class ReturnValueMixin {
    public:
        void return_value(T value)
        {
            static_cast<Promise<T>*>(this)->Parent()->StoreValue(
                std::forward<T>(value));
        }
    };

    class NurseryScopeBase {
    protected:
        NurseryScopeBase() = default;
        ~NurseryScopeBase() = default;
        OXYGEN_DEFAULT_COPYABLE(NurseryScopeBase)
        OXYGEN_DEFAULT_MOVABLE(NurseryScopeBase)
    };

    //! The promise type for a coroutine that returns T.
    template <class T>
    class Promise : public BasePromise, public ReturnValueMixin<T> {
    public:
        void unhandled_exception() const { BasePromise::Parent()->StoreException(); }

        // ReSharper disable once CppFunctionIsNotImplemented
        auto get_return_object() -> Co<T>; // Implemented in Co.h

        auto Start(TaskParent<T>* parent, Handle caller) -> Handle
        {
            return BasePromise::Start(parent, caller);
        }

        void ReParent(TaskParent<T>* parent, Handle caller)
        {
            BasePromise::ReParent(parent, caller);
        }

        /// Allows using `co_yield` instead of `co_await` for nursery factories.
        /// This is purely a syntactic trick to allow the
        /// `OXCO_WITH_NURSERY(n) { ... }` syntax to work, by making it expand
        /// to a binary operator which binds more tightly than `co_yield`.
        /// (`co_await` binds too tightly.)
        template <std::derived_from<NurseryScopeBase> U>
        auto yield_value(U&& u)
        {
            return await_transform(std::forward<U>(u));
        }

    private:
        friend ReturnValueMixin<T>;

        // ReSharper disable once CppHidingFunction
        [[nodiscard]] auto Parent() const noexcept
        {
            return static_cast<TaskParent<T>*>(BasePromise::Parent());
        }
    };

    template <>
    class ReturnValueMixin<void> {
    public:
        void return_void()
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            static_cast<Promise<void>*>(this)->Parent()->StoreSuccess();
        }
    };

    //! The promise type for a task that is not backed by a coroutine and
    //! immediately returns a value of type T when invoked. Used by `Just()`
    //! and `NoOp()`.
    template <class T>
    class StubPromise : public Promise<T> {
    public:
        explicit StubPromise(T value)
            : value_(std::forward<T>(value))
        {
            this->template MakeStub<StubPromise, &StubPromise::OnStart>(
                /* delete_this_on_destroy = */ true);
        }

    private:
        void OnStart() { this->return_value(std::forward<T>(value_)); }
        T value_;
    };
    template <>
    class StubPromise<void> : public Promise<void> {
    public:
        static auto Instance() -> StubPromise&
        {
            static StubPromise inst;
            return inst;
        }

    private:
        StubPromise()
        {
            this->MakeStub<StubPromise, &StubPromise::OnStart>(
                /* delete_this_on_destroy = */ false);
        }
        void OnStart() { this->return_void(); }
    };

    struct DestroyPromise {
        template <class T>
        void operator()(Promise<T>* p) const { p->Destroy(); }
    };

    template <class T>
    using PromisePtr = std::unique_ptr<Promise<T>, DestroyPromise>;

} // namespace detail

} // namespace oxygen::co
