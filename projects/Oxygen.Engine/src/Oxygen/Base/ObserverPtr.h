//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace oxygen {

//! Concept for types that can be dereferenced (not void)
template <typename T>
concept Dereferenceable = !std::is_void_v<T>;

//! Concept for valid observer_ptr element types
template <typename T>
concept ObservableType = !std::is_reference_v<T>;

//! Non-owning pointer vocabulary type (C++23 observer_ptr compatible)
/*!
 observer_ptr is a lightweight wrapper for raw pointers, providing pointer-like
 semantics without ownership. It is intended as a vocabulary type to clarify
 non-ownership and observation intent in APIs and data structures.

 This implementation is compatible with the C++23 std::observer_ptr
 specification and includes modern C++20/23 features like concepts and three-way
 comparison.

 @tparam T The type of the object pointed to. T shall not be a reference type,
           but may be an incomplete type or void.

 ### Type Requirements

 - T must not be a reference type
 - T may be an incomplete type
 - T may be void (but cannot be dereferenced)
 - Specializations satisfy CopyConstructible and CopyAssignable requirements

 ### Key Features

 - Non-owning: Does not manage or delete the pointed-to object.
 - Pointer semantics: Supports dereference, arrow, get, reset, release, explicit
   conversion to pointer.
 - Safe conversions: Implicitly convertible to observer_ptr<U> if T* is
   convertible to U*.
 - Comparison and swap: Supports equality, three-way comparison, nullptr
 comparison, and swap.
 - Hash support: Can be used in hash-based containers.

 ### Usage Examples

 ```cpp
 oxygen::observer_ptr<Foo> p = get_observed();
 if (p) { p->do_something(); }
 auto raw = static_cast<Foo*>(p); // explicit conversion

 // Works with hash containers
 std::unordered_set<oxygen::observer_ptr<Foo>> observer_set;

 // Supports three-way comparison (C++20)
 auto ordering = p1 <=> p2;
 ```
*/
template <typename T>
  requires ObservableType<T>
class observer_ptr {
public:
  using element_type = T;

  //! Constructs an observer_ptr that has no corresponding watched object.
  constexpr observer_ptr() noexcept
    : ptr_(nullptr)
  {
  }

  //! Constructs an observer_ptr that has no corresponding watched object.
  constexpr observer_ptr(std::nullptr_t) noexcept
    : ptr_(nullptr)
  {
  }

  //! Constructs an observer_ptr that watches p.
  constexpr explicit observer_ptr(T* p) noexcept
    : ptr_(p)
  {
  }

  //! Copy constructor
  constexpr observer_ptr(const observer_ptr&) noexcept = default;
  //! Copy assignment
  constexpr observer_ptr& operator=(const observer_ptr&) noexcept = default;

  //! Implicit conversion to observer_ptr<U> if T* convertible to U*.
  template <typename U>
    requires ObservableType<U> && std::convertible_to<T*, U*>
  constexpr operator observer_ptr<U>() const noexcept
  {
    return observer_ptr<U>(ptr_);
  }

  //! Explicit conversion to raw pointer
  constexpr explicit operator T*() const noexcept { return ptr_; }

  //! Returns a pointer to the watched object or nullptr if no object is
  //! watched.
  constexpr T* get() const noexcept { return ptr_; }

  //! Provides access to the watched object. Bihavior is undefined if get() ==
  //! nullptr.
  template <typename U = T>
  constexpr std::enable_if_t<Dereferenceable<U>, U&> operator*() const noexcept
  {
    return *ptr_;
  }

  //! Provides access to the watched pointer.
  constexpr T* operator->() const noexcept { return ptr_; }

  //! Checks whether the observed_ptr has an associated watched object, i.e.
  //! whether get() != nullptr.
  constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

  //! Reset to a new pointer (or nullptr). get() returns p after the call.
  constexpr void reset(T* p = nullptr) noexcept { ptr_ = p; }

  //! Stop watching the watched object, if any. get() returns nullptr after the
  //! call.
  /*!
   @return A pointer to the previously watched object, or nullptr if there was
   no watched object, i.e. the value which would be returned by get() before the
   call.
  */
  constexpr T* release() noexcept { return std::exchange(ptr_, nullptr); }

  //! Swap with another observer_ptr
  constexpr void swap(observer_ptr& other) noexcept
  {
    std::swap(ptr_, other.ptr_);
  }

  //! Compare with another observer_ptr
  constexpr bool operator==(const observer_ptr& other) const noexcept
  {
    return ptr_ == other.ptr_;
  }

  //! Three-way comparison with another observer_ptr
  constexpr std::strong_ordering operator<=>(
    const observer_ptr& other) const noexcept
  {
    return ptr_ <=> other.ptr_;
  }

  //! Compare with nullptr
  constexpr bool operator==(std::nullptr_t) const noexcept
  {
    return ptr_ == nullptr;
  }

private:
  T* ptr_;
};

//! Non-member swap
template <typename T>
  requires ObservableType<T>
constexpr void swap(observer_ptr<T>& a, observer_ptr<T>& b) noexcept
{
  a.swap(b);
}

//! Helper to create an observer_ptr from a raw pointer
template <typename T>
  requires oxygen::ObservableType<T>
constexpr observer_ptr<T> make_observer(T* p) noexcept
{
  return observer_ptr<T>(p);
}

//! Non-member comparison with nullptr (nullptr on left)
template <typename T>
  requires oxygen::ObservableType<T>
constexpr bool operator==(std::nullptr_t, const observer_ptr<T>& rhs) noexcept
{
  return rhs == nullptr;
}

} // namespace oxygen

//! Hash support for observer_ptr
template <typename T>
  requires oxygen::ObservableType<T>
struct std::hash<oxygen::observer_ptr<T>> {
  constexpr std::size_t operator()(
    const oxygen::observer_ptr<T>& ptr) const noexcept
  {
    return std::hash<T*> {}(ptr.get());
  }
};
