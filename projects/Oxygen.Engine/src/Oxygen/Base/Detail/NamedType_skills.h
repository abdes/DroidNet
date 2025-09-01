//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Based on NamedType, Copyright (c) 2017 Jonathan Boccara
// License: MIT
// https://github.com/joboccara/NamedType

#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>

#include <Oxygen/Base/Crtp.h>
#include <Oxygen/Base/Detail/NamedType_impl.h>

namespace oxygen {

namespace nt_detail {
  template <typename...> using void_t = void;

  template <typename NT, typename = void>
  struct has_is_hashable : std::false_type { };

  template <typename NT>
  struct has_is_hashable<NT, void_t<decltype(NT::is_hashable)>>
    : std::true_type { };

  template <typename NT, bool = has_is_hashable<NT>::value>
  struct is_hashable_helper : std::false_type { };

  template <typename NT>
  struct is_hashable_helper<NT, true>
    : std::integral_constant<bool, static_cast<bool>(NT::is_hashable)> { };

  template <typename NT>
  inline constexpr bool is_hashable_v = is_hashable_helper<NT>::value;

  // Detect if a Skills pack contains a given skill template instantiation
  template <template <typename> class Skill, typename Named>
  struct has_skill : std::false_type { };

  template <template <typename> class Skill, typename T, typename Param,
    template <typename> class... Skills>
  struct has_skill<Skill, NamedType<T, Param, Skills...>>
    : std::disjunction<std::is_same<Skill<NamedType<T, Param, Skills...>>,
        Skills<NamedType<T, Param, Skills...>>>...> { };

  // primary, overridden via partial specialization later
  template <typename NT>
  inline constexpr bool has_default_initialized_v = false;
} // namespace nt_detail

//! Provides prefix increment operator (++x) for NamedType
template <typename T>
struct OXYGEN_EBCO PreIncrementable : Crtp<T, PreIncrementable> {
  constexpr T& operator++() noexcept(noexcept(++this->underlying().get()))
  {
    ++this->underlying().get();
    return this->underlying();
  }
};

//! Provides postfix increment operator (x++) for NamedType
template <typename T>
struct OXYGEN_EBCO PostIncrementable : Crtp<T, PostIncrementable> {
  constexpr T operator++(int) noexcept(noexcept(this->underlying().get()++))
  {
    return T(this->underlying().get()++);
  }
};

//! Provides prefix decrement operator (--x) for NamedType
template <typename T>
struct OXYGEN_EBCO PreDecrementable : Crtp<T, PreDecrementable> {
  [[nodiscard]] constexpr T& operator--() noexcept(
    noexcept(--this->underlying().get()))
  {
    --this->underlying().get();
    return this->underlying();
  }
};

//! Provides postfix decrement operator (x--) for NamedType
template <typename T>
struct OXYGEN_EBCO PostDecrementable : Crtp<T, PostDecrementable> {
  [[nodiscard]] constexpr T operator--(int) noexcept(
    noexcept(this->underlying().get()--))
  {
    return T(this->underlying().get()--);
  }
};

//! Provides binary addition operators (+, +=) for NamedType
template <typename T>
struct OXYGEN_EBCO BinaryAddable : Crtp<T, BinaryAddable> {
  [[nodiscard]] constexpr T operator+(T const& other) const
    noexcept(noexcept(this->underlying().get() + other.get()))
  {
    return T(this->underlying().get() + other.get());
  }
  constexpr T& operator+=(T const& other) noexcept(
    noexcept(this->underlying().get() += other.get()))
  {
    this->underlying().get() += other.get();
    return this->underlying();
  }
};

//! Provides unary plus operator (+x) for NamedType
template <typename T> struct OXYGEN_EBCO UnaryAddable : Crtp<T, UnaryAddable> {
  [[nodiscard]] constexpr T operator+() const
    noexcept(noexcept(+this->underlying().get()))
  {
    return T(+this->underlying().get());
  }
};

//! Combines binary and unary addition operators for NamedType
template <typename T>
struct OXYGEN_EBCO Addable : BinaryAddable<T>, UnaryAddable<T> {
  using BinaryAddable<T>::operator+;
  using UnaryAddable<T>::operator+;
};

//! Provides binary subtraction operators (-, -=) for NamedType
template <typename T>
struct OXYGEN_EBCO BinarySubtractable : Crtp<T, BinarySubtractable> {
  [[nodiscard]] constexpr T operator-(T const& other) const
    noexcept(noexcept(this->underlying().get() - other.get()))
  {
    return T(this->underlying().get() - other.get());
  }
  constexpr T& operator-=(T const& other) noexcept(
    noexcept(this->underlying().get() -= other.get()))
  {
    this->underlying().get() -= other.get();
    return this->underlying();
  }
};

//! Provides unary minus operator (-x) for NamedType
template <typename T>
struct OXYGEN_EBCO UnarySubtractable : Crtp<T, UnarySubtractable> {
  [[nodiscard]] constexpr T operator-() const
    noexcept(noexcept(-this->underlying().get()))
  {
    return T(-this->underlying().get());
  }
};

//! Combines binary and unary subtraction operators for NamedType
template <typename T>
struct OXYGEN_EBCO Subtractable : BinarySubtractable<T>, UnarySubtractable<T> {
  using UnarySubtractable<T>::operator-;
  using BinarySubtractable<T>::operator-;
};

//! Provides multiplication operators (*, *=) for NamedType
template <typename T>
struct OXYGEN_EBCO Multiplicable : Crtp<T, Multiplicable> {
  [[nodiscard]] constexpr T operator*(T const& other) const
    noexcept(noexcept(this->underlying().get() * other.get()))
  {
    return T(this->underlying().get() * other.get());
  }
  constexpr T& operator*=(T const& other) noexcept(
    noexcept(this->underlying().get() *= other.get()))
  {
    this->underlying().get() *= other.get();
    return this->underlying();
  }
};

//! Provides division operators (/, /=) for NamedType
template <typename T> struct OXYGEN_EBCO Divisible : Crtp<T, Divisible> {
  [[nodiscard]] constexpr T operator/(T const& other) const
    noexcept(noexcept(this->underlying().get() / other.get()))
  {
    return T(this->underlying().get() / other.get());
  }
  constexpr T& operator/=(T const& other) noexcept(
    noexcept(this->underlying().get() /= other.get()))
  {
    this->underlying().get() /= other.get();
    return this->underlying();
  }
};

//! Provides modulo operators (%, %=) for NamedType
template <typename T> struct OXYGEN_EBCO Modulable : Crtp<T, Modulable> {
  [[nodiscard]] constexpr T operator%(T const& other) const
    noexcept(noexcept(this->underlying().get() % other.get()))
  {
    return T(this->underlying().get() % other.get());
  }
  constexpr T& operator%=(T const& other) noexcept(
    noexcept(this->underlying().get() %= other.get()))
  {
    this->underlying().get() %= other.get();
    return this->underlying();
  }
};

//! Provides bitwise inversion operator (~) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseInvertable : Crtp<T, BitWiseInvertable> {
  [[nodiscard]] constexpr T operator~() const
    noexcept(noexcept(~this->underlying().get()))
  {
    return T(~this->underlying().get());
  }
};

//! Provides bitwise AND operators (&, &=) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseAndable : Crtp<T, BitWiseAndable> {
  [[nodiscard]] constexpr T operator&(T const& other) const
    noexcept(noexcept(this->underlying().get() & other.get()))
  {
    return T(this->underlying().get() & other.get());
  }
  constexpr T& operator&=(T const& other) noexcept(
    noexcept(this->underlying().get() &= other.get()))
  {
    this->underlying().get() &= other.get();
    return this->underlying();
  }
};

//! Provides bitwise OR operators (|, |=) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseOrable : Crtp<T, BitWiseOrable> {
  [[nodiscard]] constexpr T operator|(T const& other) const
    noexcept(noexcept(this->underlying().get() | other.get()))
  {
    return T(this->underlying().get() | other.get());
  }
  constexpr T& operator|=(T const& other) noexcept(
    noexcept(this->underlying().get() |= other.get()))
  {
    this->underlying().get() |= other.get();
    return this->underlying();
  }
};

//! Provides bitwise XOR operators (^, ^=) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseXorable : Crtp<T, BitWiseXorable> {
  [[nodiscard]] constexpr T operator^(T const& other) const
    noexcept(noexcept(this->underlying().get() ^ other.get()))
  {
    return T(this->underlying().get() ^ other.get());
  }
  constexpr T& operator^=(T const& other) noexcept(
    noexcept(this->underlying().get() ^= other.get()))
  {
    this->underlying().get() ^= other.get();
    return this->underlying();
  }
};

//! Provides bitwise left shift operators (<<, <<=) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseLeftShiftable : Crtp<T, BitWiseLeftShiftable> {
  [[nodiscard]] constexpr T operator<<(T const& other) const
    noexcept(noexcept(this->underlying().get() << other.get()))
  {
    return T(this->underlying().get() << other.get());
  }
  constexpr T& operator<<=(T const& other) noexcept(
    noexcept(this->underlying().get() <<= other.get()))
  {
    this->underlying().get() <<= other.get();
    return this->underlying();
  }
};

//! Provides bitwise right shift operators (>>, >>=) for NamedType
template <typename T>
struct OXYGEN_EBCO BitWiseRightShiftable : Crtp<T, BitWiseRightShiftable> {
  [[nodiscard]] constexpr T operator>>(T const& other) const
    noexcept(noexcept(this->underlying().get() >> other.get()))
  {
    return T(this->underlying().get() >> other.get());
  }
  constexpr T& operator>>=(T const& other) noexcept(
    noexcept(this->underlying().get() >>= other.get()))
  {
    this->underlying().get() >>= other.get();
    return this->underlying();
  }
};

//! Provides comparison operators (<, >, <=, >=, ==, !=) for NamedType
template <typename T> struct OXYGEN_EBCO Comparable : Crtp<T, Comparable> {
  [[nodiscard]] constexpr bool operator<(Comparable<T> const& other) const
    noexcept(noexcept(this->underlying().get() < other.underlying().get()))
  {
    return this->underlying().get() < other.underlying().get();
  }
  [[nodiscard]] constexpr bool operator>(Comparable<T> const& other) const
    noexcept(noexcept(other.underlying().get() < this->underlying().get()))
  {
    return other.underlying().get() < this->underlying().get();
  }
  [[nodiscard]] constexpr bool operator<=(Comparable<T> const& other) const
    noexcept(noexcept(!(other < *this)))
  {
    return !(other < *this);
  }
  [[nodiscard]] constexpr bool operator>=(Comparable<T> const& other) const
    noexcept(noexcept(!(*this < other)))
  {
    return !(*this < other);
  }
  [[nodiscard]] constexpr bool operator==(Comparable<T> const& other) const
    noexcept(noexcept(!(*this < other) && !(other < *this)))
  {
    return !(*this < other) && !(other < *this);
  }
  [[nodiscard]] constexpr bool operator!=(Comparable<T> const& other) const
    noexcept(noexcept(!(*this == other)))
  {
    return !(*this == other);
  }
};

//! Provides dereference operator (*) for NamedType
template <typename T> struct OXYGEN_EBCO Dereferencable;

template <typename T, typename Parameter, template <typename> class... Skills>
struct Dereferencable<NamedType<T, Parameter, Skills...>>
  : Crtp<NamedType<T, Parameter, Skills...>, Dereferencable> {
  [[nodiscard]] constexpr T& operator*() & noexcept
  {
    return this->underlying().get();
  }
  [[nodiscard]] constexpr std::remove_reference_t<T> const&
  operator*() const& noexcept
  {
    return this->underlying().get();
  }
};

//! Provides implicit conversion capability to specified destination type
template <typename Destination> struct OXYGEN_EBCO ImplicitlyConvertibleTo {
  template <typename T> struct templ : Crtp<T, templ> {
    [[nodiscard]] constexpr operator Destination() const
      noexcept(noexcept(static_cast<Destination>(this->underlying().get())))
    {
      return this->underlying().get();
    }
  };
};

//! Provides stream output capability for NamedType
template <typename T> struct OXYGEN_EBCO Printable : Crtp<T, Printable> {
  static constexpr bool is_printable = true;

  void print(std::ostream& os) const { os << this->underlying().get(); }
};

template <typename T, typename Parameter, template <typename> class... Skills>
typename std::enable_if<NamedType<T, Parameter, Skills...>::is_printable,
  std::ostream&>::type
operator<<(std::ostream& os, NamedType<T, Parameter, Skills...> const& object)
{
  object.print(os);
  return os;
}

//! Enables std::hash support for NamedType
template <typename T> struct OXYGEN_EBCO Hashable {
  static constexpr bool is_hashable = true;
};

//! Provides function call conversion operators for NamedType
template <typename NamedType_> struct OXYGEN_EBCO FunctionCallable;

template <typename T, typename Parameter, template <typename> class... Skills>
struct FunctionCallable<NamedType<T, Parameter, Skills...>>
  : Crtp<NamedType<T, Parameter, Skills...>, FunctionCallable> {
  [[nodiscard]] constexpr operator T const&() const noexcept
  {
    return this->underlying().get();
  }
  [[nodiscard]] constexpr operator T&() noexcept
  {
    return this->underlying().get();
  }
};

//! Provides member access operator (->) for NamedType
template <typename NamedType_> struct OXYGEN_EBCO MethodCallable;

template <typename T, typename Parameter, template <typename> class... Skills>
struct MethodCallable<NamedType<T, Parameter, Skills...>>
  : Crtp<NamedType<T, Parameter, Skills...>, MethodCallable> {
  [[nodiscard]] constexpr std::remove_reference_t<T> const*
  operator->() const noexcept
  {
    return std::addressof(this->underlying().get());
  }
  [[nodiscard]] constexpr std::remove_reference_t<T>* operator->() noexcept
  {
    return std::addressof(this->underlying().get());
  }
};

//! Combines function and method call capabilities for NamedType
template <typename NamedType_>
struct OXYGEN_EBCO Callable : FunctionCallable<NamedType_>,
                              MethodCallable<NamedType_> { };

//! Combines prefix and postfix increment operators for NamedType
template <typename T>
struct OXYGEN_EBCO Incrementable : PreIncrementable<T>, PostIncrementable<T> {
  using PostIncrementable<T>::operator++;
  using PreIncrementable<T>::operator++;
};

//! Combines prefix and postfix decrement operators for NamedType
template <typename T>
struct OXYGEN_EBCO Decrementable : PreDecrementable<T>, PostDecrementable<T> {
  using PostDecrementable<T>::operator--;
  using PreDecrementable<T>::operator--;
};

//! Comprehensive arithmetic operations skill combining all arithmetic operators
/*!
 Provides a complete set of arithmetic operations for NamedType including:
 increment, decrement, addition, subtraction, multiplication, division,
 modulo, bitwise operations, comparison operators, printing, and hashing.

 @see Incrementable, Decrementable, Addable, Subtractable, Multiplicable
 @see Divisible, Modulable, Comparable, Printable, Hashable
*/
template <typename T>
struct OXYGEN_EBCO Arithmetic : Incrementable<T>,
                                Decrementable<T>,
                                Addable<T>,
                                Subtractable<T>,
                                Multiplicable<T>,
                                Divisible<T>,
                                Modulable<T>,
                                BitWiseInvertable<T>,
                                BitWiseAndable<T>,
                                BitWiseOrable<T>,
                                BitWiseXorable<T>,
                                BitWiseLeftShiftable<T>,
                                BitWiseRightShiftable<T>,
                                Comparable<T>,
                                Printable<T>,
                                Hashable<T> { };

//! Enables default initialization for NamedType instances
template <typename T>
struct OXYGEN_EBCO DefaultInitialized : Crtp<T, DefaultInitialized> {
  static constexpr bool default_initialized = true;
};

} // namespace oxygen

namespace std {
template <typename T, typename Parameter, template <typename> class... Skills>
struct hash<oxygen::NamedType<T, Parameter, Skills...>> {
  using NamedType = oxygen::NamedType<T, Parameter, Skills...>;
  size_t operator()(
    oxygen::NamedType<T, Parameter, Skills...> const& x) const noexcept
  {
    // Only usable when Hashable skill opted in
    static_assert(oxygen::nt_detail::is_hashable_v<NamedType>,
      "oxygen::NamedType is not hashable: add oxygen::Hashable");
    static_assert(
      noexcept(std::hash<T>()(x.get())), "hash function should not throw");

    return std::hash<T>()(x.get());
  }
};
} // namespace std
