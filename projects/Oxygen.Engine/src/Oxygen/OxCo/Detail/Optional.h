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

  /// A wrapper wrapping a pointer to a std::optional<Ref>-like interface.
  template <class T>
    requires(std::is_reference_v<T>)
  class OptionalRef {
    using Pointee = std::remove_reference_t<T>;

  public:
    OptionalRef() noexcept
      : ptr_(nullptr)
    {
    }
    OptionalRef(T value) noexcept
      : ptr_(&value)
    {
    }
    OptionalRef(std::nullopt_t) noexcept
      : ptr_(nullptr)
    {
    }

    auto has_value() const noexcept -> bool { return ptr_ != nullptr; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    auto operator*() noexcept -> Pointee& { return *ptr_; }
    auto operator*() const noexcept -> const Pointee& { return *ptr_; }

    auto operator->() noexcept -> Pointee* { return ptr_; }
    auto operator->() const noexcept -> const Pointee* { return ptr_; }

    auto value() -> Pointee& { return ref(); }
    auto value() const -> const Pointee& { return ref(); }

    template <class U> auto value_or(U&& def) const -> Pointee
    {
      return has_value() ? *ptr_ : static_cast<Pointee>(std::forward<U>(def));
    }

    void reset() noexcept { ptr_ = nullptr; }
    void swap(OptionalRef& other) noexcept { std::swap(ptr_, other.ptr_); }

  private:
    auto ref() const -> Pointee&
    {
      if (has_value()) {
        return *ptr_;
      } else {
        throw std::bad_optional_access();
      }
    }

  private:
    Pointee* ptr_;
  };

  class OptionalVoid {
  public:
    OptionalVoid()
      : value_(false)
    {
    }
    explicit OptionalVoid(std::in_place_t)
      : value_(true)
    {
    }
    explicit(false) OptionalVoid(std::nullopt_t)
      : value_(false)
    {
    }

    auto has_value() const noexcept -> bool { return value_; }
    explicit operator bool() const noexcept { return value_; }

    void value() const
    {
      if (!value_) {
        throw std::bad_optional_access();
      }
    }
    void operator*() const { }

    void reset() noexcept { value_ = false; }
    void swap(OptionalVoid& other) noexcept { std::swap(value_, other.value_); }

  private:
    bool value_;
  };

  template <class T> struct MakeOptional {
    using Type = std::optional<T>;
  };
  template <class T>
    requires std::is_reference_v<T>
  struct MakeOptional<T> {
    using Type = OptionalRef<T>;
  };
  template <class T>
    requires std::is_void_v<T> // also matches cv-qualified void
  struct MakeOptional<T> {
    using Type = OptionalVoid;
  };

} // namespace detail

template <class T> using Optional = typename detail::MakeOptional<T>::Type;

} // namespace oxygen::co
