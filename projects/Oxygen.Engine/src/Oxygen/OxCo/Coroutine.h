
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <coroutine>
#include <utility>

// --- Handles -----------------------------------------------------------------

namespace oxygen::co {

//! Coroutine handle type.
template <class Promise>
using CoroutineHandle = std::coroutine_handle<Promise>;

namespace detail {
    //! Type erased coroutine handle, for internal use only.
    using Handle = CoroutineHandle<void>;
} // namespace oxygen::co::detail

//! Similar to std::noop_coroutine(), but guaranteed to return the same value
//! for each invocation, so can be compared against.
inline auto NoOpHandle()
{
    static const detail::Handle ret = std::noop_coroutine();
    return ret;
}

} // namespace oxygen::co

// --- Concepts ----------------------------------------------------------------

namespace oxygen::co {

namespace detail {

    // Utility helpers for concepts below.
    template <class From, class... To>
    concept ConvertibleToAny = (std::convertible_to<From, To> || ...);

    template <class From, class... To>
    concept SameAsAny = (std::same_as<From, To> || ...);

    struct Unspecified { };

    //! Required internally to circumvent certain out-of-order definitions
    //! of `operator co_await()`.
    template <class T, class Ret>
    constexpr bool kThisIsAwaitableTrustMe = false;

} // namespace oxygen::co::detail

//! Defined the requirements of an Awaiter type as expected by the C++20
//! coroutine specification.
/*!
 An `Awaiter` is an object that interacts directly with the coroutine mechanism
 to control the suspension and resumption of the coroutine. It's obtained from
 the `Awaitable` by calling the operator `co_await` on it.

 The awaiter provides specific methods:

   - `await_ready()->bool`: Determines whether the coroutine should be
     suspended.

   - `await_suspend(std::coroutine_handle<>)->void|bool|std::coroutine_handle<>`:
     Specifies the action to take when the coroutine is suspended.
       - If the return type is `void`, control is immediately returned to the
         caller of the current coroutine (this coroutine remains suspended),
       - If the return type is `bool`, 'true' returns control to the caller of
         the current coroutine, 'false' resumes the current coroutine.
       - If the return type is `std::coroutine_handle<>`, the handle is resumed
         (by a call to handle.resume(), which may eventually chain to resuming
         the current coroutine).

   - `await_resume()`: Called (whether the coroutine was suspended or not), and
     its result is the result of the whole `co_await expr` expression.
*/
template <typename T, class Ret = detail::Unspecified>
concept Awaiter = requires(T t, const T ct, std::coroutine_handle<> h) {
    { ct.await_ready() } -> std::same_as<bool>;
    { t.await_suspend(h) } -> detail::ConvertibleToAny<void, bool, detail::Handle>;
    { std::forward<T>(t).await_resume() };
} && (std::is_same_v<Ret, detail::Unspecified> || requires(T t) {
    { std::forward<T>(t).await_resume() } -> std::convertible_to<Ret>;
});

//! Concept to define Promise type requirements with return type Ret.
template <typename P, typename Ret>
concept PromiseType = requires(P promise) {
    { promise.get_return_object() };
    { promise.initial_suspend() } -> Awaiter;
    { promise.final_suspend() } -> Awaiter;
    { promise.unhandled_exception() } -> std::same_as<void>;
} && ((std::same_as<Ret, void> && requires(P promise) {
    { promise.return_void() } -> std::same_as<void>;
}) || (!std::same_as<Ret, void> && requires(P promise) {
    { promise.return_value(std::declval<Ret>()) } -> std::same_as<void>;
}));

//! Defined the requirements of an Awaitable type obtained directly from the
//! `expr` in `co_await expr` when `expr` is produced by an initial suspend
//! point, a final suspend point, or a yield expression.
template <typename T, class Ret = detail::Unspecified>
concept DirectAwaitable = Awaiter<T, Ret>;

template <typename T, class Ret = detail::Unspecified>
concept MemberCoAwaitAwaitable = requires(T t) {
    { std::forward<T>(t).operator co_await() } -> DirectAwaitable<Ret>;
};

template <typename T, class Ret = detail::Unspecified>
concept GlobalCoAwaitAwaitable = requires(T t) {
    { operator co_await(std::forward<T>(t)) } -> Awaiter<Ret>;
};

//! Defined the requirements of the general Awaitable type, including through
//! the member function `await_transform` of the coroutine `Promise` type.
/*!
 If the current coroutine `Promise` type has a member function `await_transform`,
 then the `awaitable` is obtained by calling `promise.await_transform(expr)`. This
 allows the coroutine to customize how expressions are transformed into
 `awaitable`.
*/
template <typename T, class Ret = detail::Unspecified>
concept Awaitable
    = DirectAwaitable<T>
    || MemberCoAwaitAwaitable<T>
    || GlobalCoAwaitAwaitable<T>
    || detail::kThisIsAwaitableTrustMe<T, Ret>;

template <class R, class Ret = detail::Unspecified>
concept AwaitableRange = requires(R r) {
    { r.begin() == r.end() } -> std::convertible_to<bool>;
    { *r.begin() } -> Awaitable<Ret>;
};

//! @{
//! Extensions to the Awaitable concept.

class Executor;

//! An awaitable that conforms to NeedsExecutor<T> defines an
//! `await_set_executor(Executor*)` method, which will be called before
//! `await_suspend()` when the awaitable is awaited.
/*!
 It can be used to obtain a pointer to the current executor, which is useful to
 control scheduling for the awaitable itself or to propagate it to others
 that might need it.
 */
template <class T>
concept NeedsExecutor = requires(T t, Executor* ex) {
    { t.await_set_executor(ex) } noexcept -> std::same_as<void>;
};

//! @}

namespace detail {

    template <class From, class... To>
    concept same_as_any = (std::same_as<From, To> || ...);

    // Extensions for async operation cancellation.
    //
    // To make an awaitable operation cancellable after it has been suspended,
    // define await_cancel(). If its cancellation might complete asynchronously
    // (as indicated by await_cancel() returning bool rather than std::true_type),
    // define await_must_resume() to tell whether the parent's resumption
    // represents cancellation or completion of the operation modeled by the
    // awaitable.
    //
    // To change the default behavior where we can propagate a cancellation
    // instead of executing an awaitable at all, define await_early_cancel().
    // If early cancellation can fail (as indicated by await_early_cancel()
    // returning bool rather than std::true_type), define await_must_resume().
    //
    // An awaitable that does not define any of these methods will be
    // synchronously early-cancellable and not at all regular-cancellable,
    // as if it wrote:
    //     auto await_early_cancel() noexcept { return std::true_type{}; }
    //     bool await_cancel(Handle) noexcept { return false; }
    //     bool await_must_resume() const noexcept { return true; }

    // An awaitable that conforms to Cancellable<T> may be cancelled after
    // it has started running.
    template <class T>
    concept Cancellable = requires(T t, Handle h) {
        // Requests cancellation of an in-progress operation represented by this
        // awaitable. May only be called after `await_suspend()` has started
        // executing and before the handle that was passed to `await_suspend()`
        // has been resumed. (This means that if `await_suspend()` returns or
        // resumes the handle it was passed, or returns false to indicate that
        // the parent task should be resumed immediately, it is too late to call
        // `await_cancel()`.) Note that an `await_suspend()` implementation that
        // calls arbitrary user code must contend with the possibility of that
        // user code indirectly calling `await_cancel()`; for example, the user
        // code could call `cancel()` on a nursery that the awaiting task is
        // executing in.
        //
        // `await_cancel()` is passed a `std::coroutine_handle` for the parent
        // task, which will always match the handle that was originally passed
        // to `await_suspend()`. It has two choices:
        //
        // - If it can synchronously ensure that the operation will not
        //   complete, `await_cancel()` should ignore the passed-in handle and
        //   return true to indicate that the cancellation completed
        //   synchronously. Its caller is then responsible for resuming the
        //   parent task and propagating the cancellation.
        //
        // - Otherwise, `await_cancel()` must return false. This means the
        //   cancellation is in progress and its completion will be signaled by
        //   resuming the provided handle.  (It is permissible for such
        //   resumption to occur before `await_cancel()` returns.)  In this
        //   case, since coroutine resumption takes no arguments, there is a
        //   potential ambiguity between "the operation was finally cancelled"
        //   and "the operation completed normally before the requested
        //   cancellation could take effect".  See `await_must_resume()` for
        //   details on how this ambiguity is resolved.
        //
        // Mnemonic: a boolean return value from `await_cancel()` answers the
        // question "are we cancelled yet?".  Note that there's a sense in which
        // the return value meaning is opposite that of `await_suspend()`: if
        // `await_suspend()` returns false then its argument handle will be
        // resumed immediately, while for `await_cancel()` this happens if you
        // return true.
        //
        // Do not return true, or resume the provided handle, until you are
        // ready for the awaitable to be destroyed. Returning true counts as a
        // resumption, so doing both (returning true _and_ resuming the handle)
        // will produce undefined behavior.
        //
        // `await_cancel()` may return `std::true_type{}` to indicate that
        // cancellation can always complete synchronously. In addition to the
        // semantics of 'bool await_cancel(Handle) { return true; }', this
        // indicates that it is safe to propagate a pending cancellation
        // _before_ suspending the awaitable. Awaitables with this property are
        // called `Abortable`.
        { t.await_cancel(h) } noexcept -> same_as_any<bool, std::true_type>;
    };

    // An awaitable that conforms to CustomizesEarlyCancel<T> defines an
    // await_early_cancel() method to customize its handling of cancellation
    // before it has started running.
    template <class T>
    concept CustomizesEarlyCancel = requires(T t) {
        // Requests cancellation of the operation represented by this awaitable
        // before `await_suspend()` has been called. This may be called either
        // before or after `await_ready()`, and regardless of the value returned
        // by `await_ready()`.
        //
        // `await_early_cancel()` returns a bool, much like `await_cancel()`:
        //
        // - If it is fine to skip the operation represented by this awaitable
        //   in order to propagate the cancellation, return true. Neither
        //   `await_suspend()` nor `await_resume()` will be called in this case.
        //
        // - If it is important to start the operation, return false. In this
        //   case it is recommended that the awaitable remember that
        //   cancellation was requested, and attempt to prevent the underlying
        //   operation from taking unbounded time. (No further call to
        //   `await_cancel()` will be made, even if `await_early_cancel()`
        //   returns false.)  As with `await_cancel()`, when the parent task is
        //   resumed after `await_early_cancel()` returns false, the
        //   `await_must_resume()` method will be called to disambiguate between
        //   operation cancellation and operation completion.
        //
        // An implementation of this method is not required. **If no
        // `await_early_cancel()` method is provided, early cancel is assumed to
        // always succeed.** That is, the behavior is as if you wrote:
        // ```
        // auto await_early_cancel() { return std::true_type{}; }
        // ```
        // Another way to think about this is that there is a cancellation point
        // before every awaitable by default, but the awaitable can disable it
        // if desired.
        { t.await_early_cancel() } noexcept -> same_as_any<bool, std::true_type>;
    };

    // An awaitable that conforms to CustomizesMustResume<T> defines an
    // await_must_resume() method to disambiguate between cancellation and
    // completion of the underlying operation after a cancellation request
    // was not immediately successful.
    template <class T>
    concept CustomizesMustResume = requires(const T ct) {
        // Hook that will be invoked when the parent task is resumed after a
        // call to `await_cancel()` or `await_early_cancel()` that does not
        // complete synchronously, in order to determine whether the resumption
        // indicates that the operation was cancelled (false) or that it
        // completed despite our cancellation request (true).
        //
        // Note that completing with an exception is considered a type of
        // completion for this purpose. `await_resume()` will be called to
        // propagate the result of this operation if and only if
        // `await_must_resume()` returns true.
        //
        // If a call `await_early_cancel()` returns false, and no call is made
        // to `await_suspend()` because `await_ready()` is already true, then
        // `await_must_resume()` will still be called to decide whether to
        // consume the value or not. This is something of a degenerate case --
        // there's no reason for `await_early_cancel()` to return false if
        // `await_suspend()` is not required and the awaitable result is safe to
        // discard -- but forbidding it causes too many problems.
        //
        // If the operation technically did complete, but will not have any
        // observable effects until `await_resume()` is called, then its
        // awaitable may still choose to return false from
        // `await_must_resume()`.  Doing so allows us to "pretend it never
        // happened" in order to return fewer extraneous results from anyOf() or
        // oneOf() combiners.
        //
        // A definition of `await_must_resume()` is required whenever an
        // `await_cancel()` or `await_early_cancel()` method is capable of
        // returning false:
        // - if Cancellable<T> but not Abortable<T>, or
        // - if not Skippable<T> (which implies CustomizesEarlyCancel<T>, since
        //   skippable is the default) If neither cancellation method can return
        //   false, then there is no circumstance under which
        //   await_must_resume() would be invoked, so it need not be defined.
        //
        // `await_must_resume()` may return `std::false_type{}` to indicate that
        // it is always safe to discard its result after a cancellation was
        // requested. An awaitable with this property is not necessarily
        // `Abortable` -- it may still need to do some asynchronous work to
        // unwind itself after receiving a cancellation request -- but the
        // awaitable will ultimately be able to propagate the cancellation
        // instead of producing a result that its parent needs to handle.
        { ct.await_must_resume() } noexcept -> same_as_any<bool, std::false_type>;
    };

    template <class T>
    concept CanRequireResume = (!Cancellable<T> && !CustomizesMustResume<T>) || requires(const T ct) {
        { ct.await_must_resume() } noexcept -> std::same_as<bool>;
    };

    // ImplicitlySkippable<T>: awaitable of type T may be cancelled before it
    // has started running, by simply ignoring it; if we never call
    // await_suspend() or await_resume(), it's assumed that it's as if the
    // operation never happened.
    template <class T>
    concept ImplicitlySkippable = !CustomizesEarlyCancel<T>;

    // Skippable<T>: awaitable of type T can always be destroyed before
    // suspension in order to safely skip the operation it represents (to
    // propagate a cancellation instead). True if either await_early_cancel()
    // always returns true, or await_early_cancel() is not defined.
    template <class T>
    concept Skippable = ImplicitlySkippable<T> || requires(T t) {
        { t.await_early_cancel() } noexcept -> std::same_as<std::true_type>;
    };

    // Abortable<T>: awaitable of type T can always be synchronously cancelled
    // after suspension, i.e.: at any instant, if it hasn't completed, you can
    // immediately guarantee that it won't complete in the future. True if
    // await_cancel() always returns true.
    template <class T>
    concept Abortable = requires(T t, Handle h) {
        { t.await_cancel(h) } noexcept -> std::same_as<std::true_type>;
    };

    // CancelAlwaysSucceeds<T>: awaitable of type T, that has resumed its parent
    // after a cancellation was requested, can always have its result ignored in
    // order to propagate a cancellation instead. This may mean either "the
    // cancellation always succeeds, in the sense of causing the operation to
    // not produce a result, but it might not occur synchronously", or "the
    // operation might produce a result after cancellation, but nothing is lost
    // if this result is ignored".
    template <class T>
    concept CancelAlwaysSucceeds = (Skippable<T> && Abortable<T>) || requires(const T ct) {
        { ct.await_must_resume() } noexcept -> std::same_as<std::false_type>;
    };

    //
    // Non-cancellation-related extensions to the awaitable protocol. All of
    // these are independent and optional.
    //

    // An awaitable that conforms to NeedsExecutor<T> defines an
    // await_set_executor(Executor*) method, which will be called before
    // await_suspend() when the awaitable is awaited. It can be used to obtain a
    // pointer to the current executor, which is useful to control scheduling
    // for the awaitable itself or to propagate it to other awaitables that
    // might need it.
    template <class T>
    concept NeedsExecutor = requires(T t, Executor* ex) {
        { t.await_set_executor(ex) } noexcept -> std::same_as<void>;
    };

    // Required relationships between the above:
    template <class T>
    concept ValidDirectAwaitable = DirectAwaitable<T> &&

        // If await_cancel() is defined (Cancellable),
        // then either it must always return true (Abortable),
        //          or there must be an await_must_resume() to tell what to do
        //             when it returns false (CustomizesMustResume).
        //
        // (If you might not complete a cancellation immediately, then you need
        // to specify how to tell whether it completed later.)
        (!Cancellable<T> || Abortable<T> || CustomizesMustResume<T>) &&

        // If await_early_cancel() is defined (CustomizesEarlyCancel),
        // then either it must always return true (Skippable),
        //          or there must be an await_must_resume() to tell what to do
        //             when it returns false (CustomizesMustResume).
        //
        // (If you might not complete an early-cancel immediately, then you need
        // to specify how to tell whether it completed later.)
        (!CustomizesEarlyCancel<T> || Skippable<T> || CustomizesMustResume<T>) &&

        // await_must_resume() should only be capable of returning true
        // (CanRequireResume) if there's any situation where it would be
        // called. If await_cancel() and await_early_cancel() both always
        // return true, then await_must_resume() is unreachable.
        // (We allow a provably unreachable await_must_resume() that
        // always returns false_type, because it's useful to not have to
        // conditionally delete the method when wrapping a generic
        // awaitable.)
        ((CancelAlwaysSucceeds<T> && !CanRequireResume<T>) || (!CancelAlwaysSucceeds<T> && CanRequireResume<T>)) &&

        // await_must_resume() should only be defined if await_cancel() or
        // await_early_cancel() (or both) are defined.
        (Cancellable<T> || CustomizesEarlyCancel<T> || !CustomizesMustResume<T>) &&

        // For each of our extension methods for the awaitable interface:
        // if a method with that name is defined, make sure it has the
        // correct signature.
        (Cancellable<T> || !requires { &T::await_cancel; })
        && (CustomizesEarlyCancel<T> || !requires { &T::await_early_cancel; })
        && (CustomizesMustResume<T> || !requires { &T::await_must_resume; })
        && (NeedsExecutor<T> || !requires { &T::await_set_executor; });
} // namespace detail

} // namespace oxygen::co
