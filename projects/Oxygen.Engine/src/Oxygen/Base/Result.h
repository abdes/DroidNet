//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/*!
  \file Result.h
  \brief Defines the Result class template for representing the outcome of an
  operation.

  The Result class template is a C++ implementation of a common pattern
  used to represent the outcome of an operation that can either succeed or fail.
  This pattern is often referred to as the "Result" or "Outcome" pattern and is
  widely used in various programming languages, including Rust, Swift, and
  others.

  Key Features
  - **Type Safety**: The Result class uses std::variant to hold either a value
    of type T or an std::error_code. This ensures that the result is always in a
    valid state and can only be one of the two types.
  - **Error Handling**: By encapsulating the error state within the Result
    class, it provides a clear and explicit way to handle errors, avoiding the
    need for exceptions or error codes scattered throughout the code.
  - **Move Semantics**: The class supports move semantics, which is important
    for performance, especially when dealing with large or non-trivially
    copyable types.
  - **Specialization for void**: The specialization for void allows the pattern
    to be used for functions that do not return a value but still need to
    indicate success or failure.

  Fit with C++ Programming Model The Result pattern fits well with the C++
  programming model for several reasons:
  - **RAII and Resource Management**: C++ emphasizes Resource Acquisition Is
    Initialization (RAII) for resource management. The Result class can be used
    to manage resources by ensuring that resources are only acquired when the
    operation is successful.
  - **Type Safety and Strong Typing**: C++ encourages type safety and strong
    typing. The Result class leverages these principles by using std::variant to
    ensure that the result is always in a valid state.
  - **Performance**: C++ is known for its performance characteristics. The
    Result class supports move semantics, which helps in maintaining performance
    by avoiding unnecessary copies.

  Future Evolution Towards C++23 and Later The C++ standard is continuously
  evolving, and several features in C++23 and beyond will further enhance the
  usability and performance of the Result pattern:
  - **std::expected**: C++23 introduces std::expected, which is a standardized
    version of the Result pattern. It provides a similar mechanism to represent
    success or failure and will likely become the preferred way to handle such
    cases in the future.
  - **Improved std::variant**: Future improvements to std::variant and other
    standard library components will continue to enhance the performance and
    usability of the Result class.
  - **Coroutines**: With the introduction of coroutines in C++20, the Result
    pattern can be used in conjunction with coroutines to handle asynchronous
    operations more effectively.
  - **Pattern Matching**: Future versions of C++ may introduce pattern matching,
    which will make it easier to work with std::variant and similar types,
    further simplifying the use of the Result pattern.
*/

#include <system_error>
#include <variant>

namespace oxygen::serio {

//! A template class to represent the result of an operation.
/*!
  The Result class encapsulates a value of type T or an error code. It
  provides methods to check if the operation was successful and to retrieve
  the value or error.
*/
template <typename T>
class Result {
public:
    //! Constructor that initializes the Result with a value.
    explicit(false) constexpr Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(value))
    {
    }

    //! Constructor that initializes the Result with an error code.
    explicit(false) Result(std::error_code error) noexcept
        : value_(error)
    {
    }

    //! Checks if the Result contains a value.
    [[nodiscard]] constexpr auto has_value() const noexcept -> bool
    {
        return std::holds_alternative<T>(value_);
    }

    //! Retrieves the value from the Result as a constant reference.
    [[nodiscard]] constexpr auto value() const& -> const T&
    {
        return std::get<T>(value_);
    }

    //! Retrieves the value from the Result as an rvalue reference.
    constexpr auto value() && -> T&&
    {
        return std::get<T>(std::move(value_));
    }

    //! Moves the value from the Result.
    constexpr auto move_value() noexcept -> T
    {
        return std::get<T>(std::move(value_));
    }

    //! Retrieves the error code from the Result.
    [[nodiscard]] constexpr auto error() const -> const std::error_code&
    {
        return std::get<std::error_code>(value_);
    }

    //! Checks if the Result contains a value.
    constexpr explicit operator bool() const noexcept
    {
        return has_value();
    }

    //! @{
    //! Retrieves the value or a default value if the Result contains an error.
    /*!
      \param default_value The value to return if the Result contains an error.
    */
    template <typename U>
    constexpr auto value_or(U&& default_value) const& noexcept -> T
    {
        return has_value()
            ? value()
            : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    constexpr auto value_or(U&& default_value) && noexcept -> T
    {
        return has_value()
            ? std::move(value())
            : static_cast<T>(std::forward<U>(default_value));
    }
    //! @}

    //! @{
    //! Retrieves a reference to the contained value if the Result holds a value.
    constexpr auto operator*() const& noexcept -> const T&
    {
        return value();
    }

    constexpr auto operator*() & -> T&
    {
        return std::get<T>(value_);
    }

    constexpr auto operator*() && noexcept -> T&&
    {
        return std::move(value());
    }
    //! @}

private:
    std::variant<T, std::error_code> value_;
};

//! Specialization of the Result class for void.
/*!
  This specialization is used for operations that do not return a value but
  still need to indicate success or failure.
*/
template <>
class Result<void> {
public:
    //! Default constructor that initializes the Result with a success state.
    constexpr Result() noexcept
        : value_(std::monostate {})
    {
    }

    //! Constructor that initializes the Result with an error code.
    explicit(false) Result(std::error_code error) noexcept
        : value_(error)
    {
    }

    //! Checks if the Result indicates success.
    [[nodiscard]] constexpr auto has_value() const noexcept -> bool
    {
        return std::holds_alternative<std::monostate>(value_);
    }

    //! Retrieves the error code from the Result.
    [[nodiscard]] constexpr auto error() const -> const std::error_code&
    {
        return std::get<std::error_code>(value_);
    }

    //! Checks if the Result indicates success.
    constexpr explicit operator bool() const noexcept
    {
        return has_value();
    }

private:
    std::variant<std::monostate, std::error_code> value_;
};

//! Helper macro to evaluate and expression that returns a result, and if the
//! result has an error, propagates the error.
#define CHECK_RESULT(expr)                 \
    if (const auto result = expr; !result) \
    return result.error()

} // namespace oxygen::serio
