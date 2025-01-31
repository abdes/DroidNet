//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Detail/CallableSignature.h"
#include "Detail/IntrusiveList.h"
#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Concepts/Awaitable.h"
#include "Oxygen/OxCo/Detail/Result.h"
#include "Oxygen/OxCo/Detail/TaskParent.h"

#include <cassert>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace oxygen::co {

namespace detail {
    //! A tag type used to force explicit construction of certain types, to make
    //! it clear that they are not intended for specific use and preventing their
    //! accidental misuse.
    class TagCtor { };

    //! A tag type, used to indicate that a nursery should terminate by joining
    //! its tasks.
    struct JoinTag {
        explicit constexpr JoinTag(TagCtor) { }
    };

    //! A tag type, used to indicate that a nursery should terminate by
    //! requesting cancellation of its uncompleted tasks.
    struct CancelTag {
        explicit constexpr CancelTag(TagCtor) { }
    };

    //! The return type of a nursery body.
    /*!
     If the body of the nursery terminates (rather than looping forever), it
     must return either `oxygen::co::kJoin` or `oxygen::co::kCancel`.
    */
    using NurseryBodyRetVal = std::variant<JoinTag, CancelTag>;

    //! A tag type, used when we want to submit a task to a nursery, and suspend
    //! until the task finishes initializing.
    //! \see TaskStarted
    class TaskStartedTag {
        explicit constexpr TaskStartedTag(TagCtor) { }
    };

}; // namespace detail

//! Returning this value from a nursery body, causes it to wait until all of its
//! other tasks have completed normally.
static constexpr detail::JoinTag kJoin { detail::TagCtor {} };

//! Returning this value from a nursery body, will make it request them to
//! cancel their operations.
static constexpr detail::CancelTag kCancel { detail::TagCtor {} };

template <class Ret = void>
class TaskStarted;

//! A nursery represents a scope for a set of tasks to live in. Execution cannot
//! continue past the end of the nursery block until all the tasks that were
//! running in the nursery have completed.
/*!
 Since C++ does not support asynchronous destructors, a nursery requires special
 syntax to construct:

 \code{cpp}
    OXCO_WITH_NURSERY(n) {
      // `co::Nursery& n` defined in this scope
      n.start(...);
      co_return co::kJoin;
      // or co::kCancel, see below
    };
 \endcode

 If any task exits with an unhandled exception, all other tasks in the nursery
 will be cancelled, and the exception will be rethrown once the tasks in the
 nursery have completed. If multiple tasks exit with unhandled exceptions, only
 the first exception will propagate.

 The body of the nursery block is the first task that runs in the nursery. Be
 careful defining local variables within this block; they will be destroyed when
 this initial task completes, but other tasks may still be running. Anything
 that you intend to make available to other tasks in the nursery should be
 declared _outside_ the nursery block so that its scope covers the entire
 nursery.

 The initial task that forms the nursery block must end by returning either
 `co::kJoin` or `co::kCancel`. `join` will wait for all tasks in the nursery to
 exit normally; `cancel` will cancel the remaining tasks. Note that in the
 latter case, after the cancellation request is forwarded to the tasks, the
 nursery will still wait for them to finish.

 Tasks do not need to be spawned from directly within the nursery block; you can
 pass the nursery reference to another function, store a pointer, etc, and use
 the nursery's `start()` method to start new tasks from any context that has
 access to the reference. Once all tasks in the nursery have exited, execution
 will proceed in the nursery's parent task, meaning the nursery will be
 destroyed and any attempt to spawn new tasks will produce undefined behavior.
 To avoid lifetime issues from unexpected nursery closure, you should be careful
 not to preserve a reference/pointer to the nursery outside the lifetime of some
 specific task in the nursery.
*/
class Nursery : private detail::TaskParent<void> {
    template <class Ret>
    class StartAwaitableBase;
    template <class Ret, class Callable, class... Args>
    class StartAwaitable;
    template <class Ret>
    friend class TaskStarted;
    friend auto OpenNursery(Nursery*&, TaskStarted<>) -> Co<>;

public:
    struct Factory;

//! A nursery construction macro.
/*!
 Example:
 \code{cpp}
    OXCO_WITH_NURSERY(n)
    {
        // ...n.start(...)...
        co_return co::kJoin;
    };
 \endcode
 */
// NOLINTBEGIN(bugprone-macro-parentheses)
#define OXCO_WITH_NURSERY(arg_name)                                                    \
    co_yield ::oxygen::co::Nursery::Factory {} % [&](::oxygen::co::Nursery & arg_name) \
        -> ::oxygen::co::Co<::oxygen::co::detail::NurseryBodyRetVal>
    // NOLINTEND(bugprone-macro-parentheses)

    virtual ~Nursery() { DCHECK_F(tasks_.Empty()); }

    OXYGEN_MAKE_NON_COPYABLE(Nursery)

    auto operator=(Nursery&&) -> Nursery& = delete;

    [[nodiscard]] auto TaskCount() const noexcept -> size_t { return task_count_; }

    //! Starts a task in the nursery, that runs `co_await std::invoke(c, args...)`.
    /*!
     \tparam Callable The type of the callable to start.
     \tparam Args The types of the arguments to pass to the callable.
     \param callable The callable to start.
     \param args The arguments to pass to the callable.

     The callable and its arguments will be moved into storage that lives as
     long as the new task does. You can wrap arguments in `std::ref()` or
     `std::cref()` if you want to actually pass by reference. You must however,
     ensure that the referent will live long enough.
    */
    template <class Callable, class... Args>
        requires(std::invocable<Callable, Args...>
            && Awaitable<std::invoke_result_t<Callable, Args...>>
            && !std::invocable<Callable, Args..., detail::TaskStartedTag>)
    void Start(Callable callable, Args... args);

    //! Starts a task in the nursery, that runs `co_await std::invoke(c,
    //! args...)`, but  allowing the task to notify the starter of the
    //! successful initialization of the task.
    /*!
     \copydetails Start(Callable, Args...)
     \tparam Ret The return type of the `TaskStarted` callable.
     \see TaskStarted
    */
    template <class Ret = detail::Unspecified, class Callable, class... Args>
        requires(std::invocable<Callable, Args..., detail::TaskStartedTag>
            && Awaitable<std::invoke_result_t<Callable, Args..., detail::TaskStartedTag>>)
    auto Start(Callable callable, Args... args) -> Awaitable auto;

    /// Requests cancellation of all tasks.
    void Cancel();

    //! Returns the executor for this nursery.
    /*!
     \return The executor for this nursery, or `nullptr` if the nursery is
     closed (meaning no new tasks can be started in it).
     */
    [[nodiscard]] auto GetExecutor() const noexcept { return executor_; }

    template <class Callable>
    class Scope;

protected:
    template <class NurseryT>
    class Awaitable;
    template <class Derived>
    class ParentAwaitable;

    Nursery() = default;
    Nursery(Nursery&&) = default;

    void DoStart(detail::Promise<void>* p) { AddPromise(p, this).resume(); }

    void RethrowException() const;
    static auto CancellationRequest() -> std::exception_ptr;

    template <class Ret>
    auto AddPromise(detail::Promise<Ret>* promise, TaskParent<Ret>* parent) -> detail::Handle;

    template <class Ret>
    auto AddTask(Co<Ret> task, TaskParent<Ret>* parent) -> detail::Handle
    {
        return AddPromise(task.Release(), parent);
    }

    //! @{
    //! TaskParent implementation.

    auto Continuation(detail::BasePromise* promise) noexcept -> detail::Handle override;
    void StoreException() override;

    //! @}

    void DoCancel();

    template <class Callable, class... Args>
    auto MakePromise(Callable callable, Args... args) -> detail::Promise<void>*;

    void Adopt(detail::BasePromise* promise);

    template <class Ret>
    static auto MakeTaskStarted(StartAwaitableBase<Ret>* parent) -> TaskStarted<Ret>
    {
        return TaskStarted<Ret>(parent);
    }

    using TaskList = detail::IntrusiveList<detail::BasePromise>;
    TaskList tasks_;
    Executor* executor_ = nullptr;
    size_t task_count_ = 0;
    unsigned pending_task_count_ = 0;
    detail::Handle parent_ = nullptr;
    std::exception_ptr exception_;
};

//! Use to pass an argument to a callable spawned into a nursery, which can be
//! used to indicate that it has started running.
/*!
 Sometimes it may be necessary to submit a task into a nursery and suspend until
 the task finishes initializing. While it is possible to express this using
 existing synchronization primitives:

 \code{cpp}
 co::Event started;
 OXCO_WITH_NURSERY(n) {
     n.Start([&]() -> co::Co<> {
         // ...initialize...
         started.Trigger();
         // ...work...
     });
     co_await started;
     // ...communicate with the task...
 };
 \endcode

 The pattern is common enough that there is also a built-in option: the callable
 passed to `Nursery::Start()` may take a trailing `co::TaskStarted` parameter
 and invoke it after initialization. When `Nursery::Start()` receives such a
 callable, it returns an awaitable such that `co_await nursery.start(...)` will
 both start the task and wait for its initialization to complete. Using this
 feature, the above example looks like:

 \code{cpp}
 OXCO_WITH_NURSERY(n) {
     co_await n.Start([&](co::TaskStarted<> started) -> co::Co<> {
         // ...initialize...
         started();
         // ...work...
     });
     // ...communicate with the task...
 };
 \endcode

 The awaitable will also submit the task if it is destroyed before it is
 awaited, so `n.Start()` without `co_await` will work as it did without the
 `TaskStarted` parameter. If you want to support using the same function with
 both `n.Start()` and with direct `co_await`, you can let it accept a final
 argument of `co::TaskStarted<> started = {}`, with a default argument value.
 The default-constructed `TaskStarted` object performs no operation when it is
 called.

 When using this feature, the `TaskStarted` object is constructed internally by
 the nursery, and will be passed to the function after all user-specified
 arguments. This convention makes it difficult to combine use of `TaskStarted`
 with default arguments.

 A task submitted into a nursery may also communicate a value back to its
 starter, which becomes the result of the `co_await n.Start()` expression:

 \code{cpp}
 OXCO_WITH_NURSERY(n) {
     int v = co_await n.start([&](co::TaskStarted<int> started) -> co::Co<> {
         started(42);
         // ...further work...
     });
     assert(v == 42);
 };
 \endcode
*/
template <class Ret>
class TaskStarted {
    using ResultType = Ret;

    explicit TaskStarted(Nursery::StartAwaitableBase<Ret>* parent)
        : parent_(parent)
    {
    }

    friend Nursery;

public:
    TaskStarted() = default;
    ~TaskStarted() = default;

    TaskStarted(TaskStarted&& rhs) noexcept
        : parent_(std::exchange(rhs.parent_, nullptr))
    {
    }
    auto operator=(TaskStarted&& rhs) noexcept -> TaskStarted& = delete;

    TaskStarted(const TaskStarted&) = delete;
    auto operator=(TaskStarted rhs) -> TaskStarted&
    {
        std::swap(parent_, rhs.parent_);
        return *this;
    }

    void operator()(detail::ReturnType<Ret> ret)
        requires(!std::is_same_v<Ret, void>);

    void operator()()
        requires(std::is_same_v<Ret, void>);

    // Required for constraints on `Nursery::Start()`.
    // ReSharper disable once CppFunctionIsNotImplemented
    explicit(false) TaskStarted(detail::TaskStartedTag); // not defined

protected:
    Nursery::StartAwaitableBase<Ret>* parent_ = nullptr;
};

//! Usable for implementing live objects, if the only things needed
//! from their `Run()` methods is establishing a nursery.
/*!
 Example:
 \code{cpp}
     class MyLiveObject {
         co::Nursery* nursery_;
       public:
         auto Run() { return co::OpenNursery(nursery_); }
         void StartStuff() { nursery_->Start(DoStuff()); }
     };
 \endcode

 The nursery pointer passed as an argument will be initialized once the nursery
 is opened, and reset to `nullptr` when the nursery is closed.

 \note Does not return until cancelled.
*/
auto OpenNursery(Nursery*& ptr, TaskStarted<> started = {}) -> Co<>;

template <class Ret>
class Nursery::StartAwaitableBase : protected TaskParent<void> {
    friend TaskStarted<Ret>;

public:
    explicit StartAwaitableBase(Nursery* nursery)
        : nursery_(nursery)
    {
    }

    StartAwaitableBase(StartAwaitableBase&& rhs) noexcept
        : nursery_(std::exchange(rhs.nursery_, nullptr))
    {
        DCHECK_F(handle_ == NoOpHandle() && !promise_);
    }

    auto operator=(StartAwaitableBase&&) -> StartAwaitableBase& = delete;

    OXYGEN_MAKE_NON_COPYABLE(StartAwaitableBase)

private:
    void StoreSuccess() override
    {
        ABORT_F("Nursery task completed without signalling readiness");
    }

    //! Called when the task completes with an exception. Stores the exception
    //! in the internal `Result<T>` object.
    void StoreException() override
    {
        this->result_.StoreException();
    }

    //! Called when the task is cancelled. Marks the internal `Result<T>` as
    //! cancelled.
    void Cancelled() override
    {
        this->result_.MarkCancelled();
    }

    auto Continuation([[maybe_unused]] detail::BasePromise* p) noexcept -> detail::Handle override
    {
        if (Nursery* n = std::exchange(nursery_, nullptr)) {
            // The task completed without calling TaskStarted<>::operator().
            --n->pending_task_count_;
        }
        return std::exchange(handle_, NoOpHandle());
    }

    void HandOff()
    {
        if (detail::Promise<void>* p = promise_.release()) {
            p->SetExecutor(nursery_->GetExecutor());
            p->ReParent(nursery_, nursery_->parent_);
            Nursery* n = std::exchange(nursery_, nullptr);
            --n->pending_task_count_;
            n->Adopt(p);
            std::exchange(handle_, NoOpHandle()).resume();
        } else {
            // TaskStarted<> was invoked before promise construction,
            // so there's nothing to hand off to the nursery yet.
            //
            // StartAwaitable::await_suspend() will take care of submitting
            // the promise properly when it becomes available.
        }
    }

protected:
    ~StartAwaitableBase() = default;

    Nursery* nursery_;
    // Yes shadows the member in TaskParent<void>, but that's fine. The deign of
    // this class is not very clean, the TaskParent<void> and Promise<void>
    // while having a Result<Ret>...
    detail::Result<Ret> result_; // NOLINT(clang-diagnostic-shadow-field)
    detail::Handle handle_ = NoOpHandle();
    detail::PromisePtr<void> promise_;
    Executor* executor_ = nullptr;
};

template <class Ret, class Callable, class... Args>
class Nursery::StartAwaitable final : public StartAwaitableBase<Ret> {
    friend Nursery;

public:
    StartAwaitable(StartAwaitable&&) = default;
    auto operator=(StartAwaitable&&) -> StartAwaitable& = delete;
    OXYGEN_MAKE_NON_COPYABLE(StartAwaitable)

    //! @{
    //! Awaitable implementation.

    // ReSharper disable CppMemberFunctionMayBeStatic
    auto await_early_cancel() noexcept -> bool { return false; }
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

    void await_set_executor(Executor* ex) noexcept { this->executor_ = ex; }

    auto await_suspend(detail::Handle h) -> detail::Handle
    {
        DLOG_F(1, "    ...Nursery::start() {}", fmt::ptr(this));
        detail::PromisePtr<void> promise(std::apply(
            [this](auto&&... args) {
                return this->nursery_->MakePromise(std::move(callable_),
                    std::move(args)...,
                    MakeTaskStarted(this));
            },
            std::move(args_)));

        if (this->result_.Completed()) {
            // TaskStarted<> was invoked before promise construction,
            // and handOff() was skipped; hand off the promise to the nursery
            // ourselves.
            std::exchange(this->nursery_, nullptr)->DoStart(promise.release());
            return h;
        } else {
            ++this->nursery_->pending_task_count_;
            this->handle_ = h;
            promise->SetExecutor(this->executor_);
            this->promise_ = std::move(promise);
            return this->promise_->Start(this, h);
        }
    }

    auto await_resume() && -> Ret { return std::move(this->result_).Value(); }

    auto await_cancel(detail::Handle) noexcept -> bool
    {
        if (this->promise_) {
            this->promise_->Cancel();
        }
        return false;
    }

    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return !this->result_.WasCancelled();
    }

    // ReSharper restore CppMemberFunctionMayBeStatic
    //! @}

    ~StartAwaitable()
    {
        if (this->nursery_) {
            std::apply(
                [this](auto&&... args) {
                    this->nursery_->Start(
                        std::move(callable_), std::move(args)...,
                        MakeTaskStarted(static_cast<StartAwaitableBase<Ret>*>(nullptr)));
                },
                std::move(args_));
        }
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - perfect forwarding
    StartAwaitable(Nursery* nursery, Callable&& callable, Args&&... args)
        : StartAwaitableBase<Ret>(nursery)
        , callable_(std::forward<Callable>(callable))
        , args_(std::forward<Args>(args)...)
    {
    }

    Callable callable_;
    std::tuple<Args...> args_;
};

template <class Ret>
void TaskStarted<Ret>::operator()(detail::ReturnType<Ret> ret)
    requires(!std::is_same_v<Ret, void>)
{
    if (auto p = std::exchange(parent_, nullptr)) {
        p->result_.StoreValue(std::forward<Ret>(ret));
        p->HandOff();
    }
}

template <class Ret>
void TaskStarted<Ret>::operator()()
    requires(std::is_same_v<Ret, void>)
{
    if (auto p = std::exchange(parent_, nullptr)) {
        p->result_.StoreSuccess();
        p->HandOff();
    }
}

/// An exception_ptr value meaning the nursery has been cancelled due to an
/// explicit request. This won't result in any exception being propagated to the
/// caller, but any further tasks spawned into the nursery will get immediately
/// cancelled.
inline auto Nursery::CancellationRequest() -> std::exception_ptr
{
    struct Tag { };
    static const std::exception_ptr ret = std::make_exception_ptr(Tag {});
    return ret;
}

inline void Nursery::RethrowException() const
{
    if (exception_ && exception_ != CancellationRequest()) {
        std::rethrow_exception(exception_);
    }
}

inline void Nursery::Adopt(detail::BasePromise* promise)
{
    DCHECK_NOTNULL_F(executor_, "Nursery is closed to new arrivals");
    DLOG_F(1, "pr {} handed to nursery {} ({} tasks total)",
        fmt::ptr(promise), fmt::ptr(this), task_count_ + 1);
    if (exception_) {
        promise->Cancel();
    }
    tasks_.PushBack(*promise);
    ++task_count_;
}

template <class Ret>
auto Nursery::AddPromise(detail::Promise<Ret>* promise,
    TaskParent<Ret>* parent) -> detail::Handle
{
    DCHECK_NOTNULL_F(promise);
    Adopt(promise);
    promise->SetExecutor(executor_);
    return promise->Start(parent, parent_);
}

namespace detail {
    //! Has member field `value` that resolves to `true` if T is a template
    //! instantiation of Template, and `false` otherwise.
    template <template <typename...> typename Template, typename T>
    struct IsSpecializationOf : std::false_type { };

    template <template <typename...> typename Template, typename... Args>
    struct IsSpecializationOf<Template, Template<Args...>> : std::true_type { };

    // ReSharper disable once CppInconsistentNaming
    template <template <typename...> typename Template, typename T>
    constexpr inline bool IsSpecializationOf_v = IsSpecializationOf<Template, T>::value;

    // ReSharper disable once CppInconsistentNaming
    template <typename T>
    constexpr bool IsReferenceWrapper_v = IsSpecializationOf_v<std::reference_wrapper, T>;
} // namespace detail

template <class Callable, class... Args>
auto Nursery::MakePromise(Callable callable, Args... args) -> detail::Promise<void>*
{
    Co<> ret;
    if constexpr ((std::is_reference_v<Callable> && std::is_invocable_r_v<Co<>, Callable>)
        || std::is_convertible_v<Callable, Co<> (*)()>) {
        // The awaitable is an async lambda (lambda that produces a Co<>) and
        // it either was passed by `lvalue reference` or it is stateless, and no
        // arguments were supplied. In this case, we don't have to worry about
        // the lifetime of its captures, and can thus save an allocation here.
        ret = callable();
    } else {
        // The lambda has captures, or we're working with a different awaitable
        // type, so wrap it into another async function. The contents of the
        // awaitable object (such as the lambda captures) will be kept alive as
        // an argument of the new async function.

        // Note: cannot use `std::invoke()` here, as any temporaries created
        // inside it will be destroyed before `invoke()` returns. We need
        // the function call and `co_await` inside one statement, so mimic
        // `std::invoke()` logic here.
        if constexpr (std::is_member_pointer_v<Callable>) {
            ret = []<typename Object>(Callable c, Object obj, auto... a) -> Co<> {
                if constexpr (std::is_pointer_v<Object>) {
                    co_await (obj->*c)(std::move(a)...);
                } else if constexpr (detail::IsReferenceWrapper_v<Object>) {
                    co_await (obj.get().*c)(std::move(a)...);
                } else {
                    co_await (std::move(obj).*c)(std::move(a)...);
                }
            }(std::move(callable), std::move(args)...);
        } else {
            ret = [](Callable c, Args... a) -> Co<> {
                co_await (std::move(c))(std::move(a)...);
            }(std::move(callable), std::move(args)...);
        }
    }

    return ret.Release();
}

template <class Callable, class... Args>
    requires(std::invocable<Callable, Args...>
        && Awaitable<std::invoke_result_t<Callable, Args...>>
        && !std::invocable<Callable, Args..., detail::TaskStartedTag>)
void Nursery::Start(Callable callable, Args... args)
{
    DoStart(MakePromise(std::forward<Callable>(callable),
        std::forward<Args>(args)...));
}

template <class Ret /* = detail::Unspecified*/, class Callable, class... Args>
    requires(std::invocable<Callable, Args..., detail::TaskStartedTag>
        && Awaitable<std::invoke_result_t<Callable, Args..., detail::TaskStartedTag>>)
auto Nursery::Start(Callable callable, Args... args) -> co::Awaitable auto
{
    if constexpr (std::is_same_v<Ret, detail::Unspecified>) {
        using Sig = detail::CallableSignature<Callable>;
        using TaskStartedArg = typename Sig::template Arg<Sig::Arity - 1>;
        using ResultType = typename TaskStartedArg::ResultType;
        return StartAwaitable<ResultType, Callable, Args...>(
            this, std::move(callable), std::move(args)...);
    } else {
        return StartAwaitable<Ret, Callable, Args...>(this, std::move(callable),
            std::move(args)...);
    }
}

inline void Nursery::DoCancel()
{
    if (!executor_ || tasks_.Empty()) {
        return;
    }

    // Task cancellation may modify tasks_ arbitrarily,
    // invalidating iterators to task being cancelled or its
    // neighbors, thereby making it impossible to traverse through
    // tasks_ safely; so defer calling cancel() through the executor.
    Executor* executor = executor_;
    executor->Capture(
        [this] {
            for (detail::BasePromise& t : tasks_) {
                executor_->Schedule(
                    +[](detail::BasePromise* p) noexcept {
                        p->Cancel();
                    },
                    &t);
            }
        },
        task_count_);

    executor->RunSoon();
}

inline void Nursery::Cancel()
{
    if (exception_) {
        return; // already cancelling
    }
    DLOG_F(1, "nursery {} cancellation requested", fmt::ptr(this));
    if (!exception_) {
        exception_ = CancellationRequest();
    }
    DoCancel();
}

inline void Nursery::StoreException()
{
    if (parent_ == nullptr) {
        // This is an UnsafeNursery that has not been Join()'ed. There is no
        // one we can pass our exception to, so we have no choice but to...
        std::terminate();
    }
    const bool need_cancel = (!exception_);
    if (!exception_ || exception_ == CancellationRequest()) {
        exception_ = std::current_exception();
    }
    if (need_cancel) {
        DoCancel();
    }
}

inline auto Nursery::Continuation(detail::BasePromise* promise) noexcept -> detail::Handle
{
    DLOG_F(1, "pr {} done in nursery {} ({} tasks remaining)",
        fmt::ptr(promise), fmt::ptr(this), task_count_ - 1);
    TaskList::Erase(*promise);
    --task_count_;

    Executor* executor = executor_;
    detail::Handle ret = NoOpHandle();
    // NB: in an UnsafeNursery, parent_ is the task that called Join(), or
    // nullptr if no one has yet
    if (tasks_.Empty() && pending_task_count_ == 0 && parent_ != nullptr) {
        ret = std::exchange(parent_, nullptr);
        executor_ = nullptr; // nursery is now closed
    }

    // Defer promise destruction to the executor, as this may call
    // scope guards, essentially interrupting the coroutine which called
    // Nursery::Cancel().
    executor->RunSoon(
        +[](detail::BasePromise* p) noexcept { p->Destroy(); }, promise);

    // To be extra safe, defer the Resume() call to the executor as well,
    // so we can be sure we don't resume the parent before destroying the frame
    // of the last child.
    if (ret != NoOpHandle()) {
        executor->RunSoon(
            +[](void* arg) noexcept { detail::Handle::from_address(arg).resume(); },
            ret.address());
    }
    return std::noop_coroutine();
}

template <class Derived>
class Nursery::ParentAwaitable {

    [[nodiscard]] auto Self() -> Derived& { return static_cast<Derived&>(*this); }
    [[nodiscard]] auto Self() const -> const Derived& { return static_cast<const Derived&>(*this); }

public:
    auto await_early_cancel() noexcept
    {
        Self().nursery_.Cancel();
        return false;
    }

    auto await_cancel(detail::Handle) noexcept
    {
        Self().nursery_.Cancel();
        return false;
    }
    void await_resume()
    {
        DLOG_F(1, "nursery {} done", fmt::ptr(&Self().nursery_));
        Self().nursery_.RethrowException();
    }

    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return Self().nursery_.exception_ != CancellationRequest();
    }

private:
    // CRTP: Constructor is private, and Derived is a friend
    ParentAwaitable() = default;
    friend Derived;
};

template <class NurseryT>
class Nursery::Awaitable
    : public ParentAwaitable<Awaitable<NurseryT>> {
    friend ParentAwaitable<Awaitable>;
    friend NurseryT;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    NurseryT& nursery_;

    explicit Awaitable(NurseryT& nursery)
        : nursery_(nursery)
    {
    }

public:
    ~Awaitable()
        requires(std::derived_from<NurseryT, Nursery>)
    = default;
    OXYGEN_DEFAULT_MOVABLE(Awaitable)
    OXYGEN_DEFAULT_COPYABLE(Awaitable)

    [[nodiscard]] auto await_ready() const noexcept { return nursery_.executor_ == nullptr; }

    auto await_suspend(detail::Handle h) -> bool
    {
        DCHECK_F(!nursery_.parent_);
        if (nursery_.tasks_.Empty()) {
            // Just close the nursery, don't actually suspend
            nursery_.executor_ = nullptr;
            return false;
        }
        nursery_.parent_ = h;
        return true;
    }
};

template <class Callable>
class Nursery::Scope final : public detail::NurseryScopeBase,
                             public ParentAwaitable<Scope<Callable>>,
                             private TaskParent<detail::NurseryBodyRetVal> {
    class Impl final : public Nursery {
    public:
        Impl() = default;
        ~Impl() override = default;
        OXYGEN_DEFAULT_MOVABLE(Impl)
        OXYGEN_DEFAULT_COPYABLE(Impl)
    };

public:
    explicit Scope(Callable&& c)
        : callable_(std::move(c))
    {
    }
    virtual ~Scope() = default;
    OXYGEN_DEFAULT_MOVABLE(Scope)
    OXYGEN_DEFAULT_COPYABLE(Scope)

    void await_set_executor(Executor* ex) noexcept { nursery_.executor_ = ex; }

    // ReSharper disable once CppMemberFunctionMayBeStatic
    [[nodiscard]] auto await_ready() const noexcept { return false; }

    auto await_suspend(detail::Handle h) -> detail::Handle
    {
        nursery_.parent_ = h;
        Co<detail::NurseryBodyRetVal> body = callable_(nursery_);
        DLOG_F(1, "    ... nursery {} starting with task {}",
            fmt::ptr(&nursery_), fmt::ptr(body.promise_.get()));
        return nursery_.AddTask(std::move(body), this);
    }

private:
    void StoreValue(const detail::NurseryBodyRetVal value) override
    {
        if (std::holds_alternative<detail::CancelTag>(value)) {
            nursery_.Cancel();
        }
    }
    void StoreException() override { nursery_.StoreException(); }

    auto Continuation(detail::BasePromise* promise) noexcept -> detail::Handle override
    {
        return nursery_.Continuation(promise);
    }

    friend ParentAwaitable<Scope>; // so it can access nursery_
    [[no_unique_address]] Callable callable_;
    Impl nursery_;
};

struct Nursery::Factory {
    template <class Callable>
    auto operator%(Callable&& c)
    {
        return Scope<Callable>(std::forward<Callable>(c));
    }
};

namespace detail {
    class BackReferencedNursery final : public Nursery {
        friend auto co::OpenNursery(Nursery*&, TaskStarted<>) -> Co<>;
        friend Awaitable<BackReferencedNursery>;

        BackReferencedNursery(Executor* executor, Nursery*& backref)
            : backref_(backref)
        {
            backref_ = this;
            executor_ = executor;
        }

        auto Continuation(detail::BasePromise* p) noexcept -> Handle override
        {
            if (task_count_ == 1 && pending_task_count_ == 0) {
                backref_ = nullptr;
            }
            return Nursery::Continuation(p);
        }

        auto Join() -> co::Awaitable<void> auto { return Awaitable(*this); }

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        Nursery*& backref_;
    };
} // namespace detail

inline auto OpenNursery(Nursery*& ptr, TaskStarted<> started /*= {}*/) -> Co<>
{
    Executor* ex = co_await GetExecutor();
    detail::BackReferencedNursery nursery(ex, ptr);
    nursery.Start([]() -> Co<> { co_await SuspendForever {}; });
    started();
    co_await nursery.Join();
}

} // namespace oxygen::co
