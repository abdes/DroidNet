//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Detail/GetAwaiter.h"
#include "Oxygen/OxCo/TaskCancelledException.h"

#include <exception>
#include <type_traits>
#include <variant>

namespace oxygen::co::detail {

//! @{
//! Helper classes to store and retrieve values of different types, allowing
//! lvalues and rvalues to appear in return types of tasks and `Awaitable`s.

//! Stores the value of type T, using move semantics, and returns a reference to
//! the stored value when unwrapped.
//! \tparam T The type of the value to store.
/*!
 This is the safest and most general implementation of the Storage class. It
 does not suffer from dangling references and access to unallocated memory
 because it fully owns the stored value. It can store both lvalues and rvalues,
 and it can store both const and non const values.
*/
template <class T>
struct Storage {
    using Type = T;
    static auto Wrap(T&& value) -> T&& { return std::move(value); }
    static auto Unwrap(T&& stored) -> T&& { return std::move(stored); }
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    static auto UnwrapCRef(const T& stored) -> const T& { return stored; }
};

//! Stores a lvalue reference to a value of type T, and returns a pointer to
//! the stored value when unwrapped.
//! \tparam T The lvalue reference type to store.
/*!
 This specialization of the Storage class is not safe against dangling
 references. It stores a pointer to the referenced value. The referenced value
 **must** outlive the Storage instance.

 Accessing the stored value after the referenced value has been destroyed leads
 to undefined behavior.
*/
template <class T>
struct Storage<T&> {
    using Type = T*;
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    static auto Wrap(T& value) -> T* { return &value; }
    static auto Unwrap(T* stored) -> T& { return *stored; }
    static auto UnwrapCRef(T* stored) -> const T& { return *stored; }
};

//! Stores a rvalue reference (temporary object) to a value of type T, and
//! returns a pointer to the stored value when unwrapped.
//! \tparam T The rvalue reference type to store.
/*!
 This specialization of the Storage class presents the highest risk of
 mismanaged lifetime. Temporary objects may be destroyed at the end of the full
 expression, leaving a dangling pointer.

 Accessing the stored value after it has been destroyed leads to undefined
 behavior.
 */
template <class T>
struct Storage<T&&> {
    using Type = T*;
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    static auto Wrap(T&& value) -> T* { return &value; }
    static auto Unwrap(T* stored) -> T&& { return std::move(*stored); }
    static auto UnwrapCRef(T* stored) -> const T& { return *stored; }
};

//! Does not store any value, and returns a dummy type when unwrapped.
template <>
struct Storage<void> {
    struct Type { };
    static auto Wrap() -> Type { return {}; }
    static void Unwrap(Type /*unused*/) { }
    static void UnwrapCRef(Type /*unused*/) { }
};

//! @}

//! A dummy type used instead of `void` when temporarily storing results
/// of `Awaitable`s, to allow void results to be stored without specialization.
struct Void { };

//! The type of values returned by tasks and `Awaitable`s, accepting `void` as a
//! possibility.
//! \tparam T The return type when not `void`.
template <class T>
using ReturnType = std::conditional_t<std::is_same_v<T, void>, Void, T>;

template <class Aw>
using AwaitableReturnType = ReturnType<decltype(std::declval<AwaiterType<Aw>>().await_resume())>;

//! A type that can hold the result of an asynchronous operation
/*!
 \tparam T The type of the returned value when there is one. Can be `void` if
 the operation does not return a value.

 The `Result` class is designed to store the outcome of an asynchronous task,
 which can be:
  - A value of type `T`
  - An exception (`std::exception_ptr`)
  - A confirmation of cancellation

 It supports storing values of various types, including `void`, and handles
 retrieval of the result and rethrowing exceptions.

 \throws TaskCancelledException when trying to get the result of an operation
 that has been cancelled.
 */
template <class T>
class Result {
public:
    //! Stores the result value when the operation completes successfully.
    /*!
     \param value The value to store.

     This method is used when the asynchronous operation has produced a value of
     type `T`. It stores the value internally using the appropriate `Storage`
     specialization.

     Cannot be used when `T` is `void` and requires that the type T either
     `move-constructible` or `copy-constructible`.
    */
    void StoreValue(ReturnType<T> value)
        requires(!std::is_same_v<T, void>
            && (std::is_move_constructible_v<T>
                || std::is_copy_constructible_v<T>))
    {
        value_.template emplace<kValue>(Storage<T>::Wrap(std::forward<T>(value)));
    }

    //! Signals successful completion of an operation that does not produce a value.
    void StoreSuccess()
        requires(std::is_same_v<T, void>)
    {
        value_.template emplace<kValue>(Storage<T>::Wrap());
    }

    //! Stores an exception that was thrown during the asynchronous operation.
    void StoreException(std::exception_ptr e)
    {
        value_.template emplace<kException>(std::move(e));
    }
    void StoreException() { StoreException(std::current_exception()); }

    //! Marks the operation as cancelled.
    /*!
     Trying to get a value or a stored exception from a cancelled operation will
     throw a `TaskCancelledException`.
     */
    void MarkCancelled() { value_.template emplace<kCancelled>(); }

    [[nodiscard]] auto Completed() const -> bool { return value_.index() != kIncomplete; }
    [[nodiscard]] auto HasValue() const -> bool { return value_.index() == kValue; }
    [[nodiscard]] auto HasException() const -> bool { return value_.index() == kException; }
    [[nodiscard]] auto WasCancelled() const -> bool { return value_.index() == kCancelled; }

    //! Retrieves the stored value, rethrows any stored exception, or throws
    //! `TaskCancelledException` if cancelled. Can only be called on a rvalue
    //! (an object that is about to be moved from).
    auto Value() && -> T
    {
        if constexpr (std::is_same_v<T, void>) {
            if (HasException()) {
                std::rethrow_exception(std::get<kException>(std::move(value_)));
            }
            if (WasCancelled()) {
                throw TaskCancelledException();
            }
            return;
        } else {
            if (HasValue()) {
                return Storage<T>::Unwrap(std::get<kValue>(std::move(value_)));
            }
            if (HasException()) {
                std::rethrow_exception(std::get<kException>(std::move(value_)));
            }
            throw TaskCancelledException();
        }
    }

protected:
    std::variant<std::monostate,
        typename Storage<T>::Type,
        std::exception_ptr,
        std::monostate>
        value_; // NOLINT(*-non-private-member-variables-in-classes)

    // Indexes of types stored in variant
    static constexpr size_t kIncomplete = 0;
    static constexpr size_t kValue = 1;
    static constexpr size_t kException = 2;
    static constexpr size_t kCancelled = 3;
};

} // namespace oxygen::co::detail
