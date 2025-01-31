//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#include <optional>
#include <type_traits>

namespace oxygen::co {

namespace detail {

    //! A wrapper wrapping a pointer to a std::optional<Ref>-like interface.
    template <class T>
        requires(std::is_reference_v<T>)
    class OptionalRef {
        using Pointee = std::remove_reference_t<T>;

    public:
        constexpr OptionalRef() noexcept
            : ptr_(nullptr)
        {
        }
        // ReSharper disable once CppNonExplicitConvertingConstructor
        // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
        constexpr OptionalRef(T value) noexcept
            : ptr_(&value)
        {
        }
        // ReSharper disable once CppNonExplicitConvertingConstructor
        // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
        constexpr OptionalRef(std::nullopt_t /*unused*/) noexcept
            : ptr_(nullptr)
        {
        }

        constexpr auto has_value() const noexcept { return ptr_ != nullptr; }
        constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

        constexpr auto operator*() noexcept -> Pointee& { return *ptr_; }
        constexpr auto operator*() const noexcept -> const Pointee& { return *ptr_; }

        constexpr auto operator->() noexcept { return ptr_; }
        constexpr auto operator->() const noexcept { return ptr_; }

        constexpr auto value() { return ref(); }
        constexpr auto value() const { return ref(); }

        template <class U>
        constexpr auto value_or(U&& def) const
        {
            return has_value() ? *ptr_ : static_cast<Pointee>(std::forward<U>(def));
        }

        void reset() noexcept { ptr_ = nullptr; }
        void swap(OptionalRef& other) noexcept { std::swap(ptr_, other.ptr_); }

    private:
        constexpr auto ref() const
        {
            if (has_value()) {
                return *ptr_;
            }
            throw std::bad_optional_access();
        }

        Pointee* ptr_;
    };

    //! Like std::conditional, but for templates.
    template <bool If,
        template <class...> class Then,
        template <class...> class Else>
    struct ConditionalTemplate {
        template <class... Args>
        using With = Then<Args...>;
    };
    template <template <class...> class Then, template <class...> class Else>
    struct ConditionalTemplate<false, Then, Else> {
        template <class... Args>
        using With = Else<Args...>;
    };
} // namespace detail

//! Provides a way to handle optional references in C++ by defining a custom
//! `OptionalRef` class and using a utility template to conditionally select
//! between `OptionalRef` and `std::optional`.
/*!
  This allows the `Optional` alias to work seamlessly with both reference and
  non-reference types.

  \note Cannot use `std::conditional_t<..., OptionalRef<T>>` because it would
        instantiate both branches before checking the condition, and
        `OptionalRef<T>` would fail instantiation for non-references.
 */
template <class T>
using Optional =
    typename detail::ConditionalTemplate<std::is_reference_v<T>,
        detail::OptionalRef,
        std::optional>::template With<T>;

} // namespace oxygen::co
