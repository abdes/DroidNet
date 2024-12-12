//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// use this inside a class declaration to make it non-copyable
#define OXYGEN_MAKE_NON_COPYABLE(Type)                                         \
  Type(const Type &) = delete;                                                 \
  auto operator=(const Type &)->Type & = delete

// use this inside a class declaration to make it non-moveable
// NOLINTBEGIN
#define OXYGEN_MAKE_NON_MOVEABLE(Type)                                         \
  Type(Type &&) = delete;                                                      \
  auto operator=(Type &&)->Type & = delete
// NOLINTEND

// use this inside a class declaration to declare default copy constructor and
// assignment operator
#define OXYGEN_DEFAULT_COPYABLE(Type)                                          \
  Type(const Type &) = default;                                                \
  auto operator=(const Type &)->Type & = default

// use this inside a class declaration to declare default move constructor and
// move assignment operator
// NOLINTBEGIN
#define OXYGEN_DEFAULT_MOVABLE(Type)                                           \
  Type(Type &&) = default;                                                     \
  auto operator=(Type &&)->Type & = default
// NOLINTEND

#define OXYGEN_DEFINE_FLAGS_OPERATORS(ENUM_TYPE)                               \
  (                                                                            \
      inline(ENUM_TYPE) &                                                      \
      operator|=((ENUM_TYPE) & a, ENUM_TYPE b) {                               \
        return reinterpret_cast<(ENUM_TYPE) &>(                                \
            reinterpret_cast<std::underlying_type_t<ENUM_TYPE> &>(a) |=        \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));              \
      } inline(ENUM_TYPE)                                                      \
      &                                                                        \
      operator&=((ENUM_TYPE) & a, ENUM_TYPE b) {                               \
        return reinterpret_cast<(ENUM_TYPE) &>(                                \
            reinterpret_cast<std::underlying_type_t<ENUM_TYPE> &>(a) &=        \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));              \
      } inline(ENUM_TYPE)                                                      \
      &                                                                        \
      operator^=((ENUM_TYPE) & a, ENUM_TYPE b) {                               \
        return reinterpret_cast<(ENUM_TYPE) &>(                                \
            reinterpret_cast<std::underlying_type_t<ENUM_TYPE> &>(a) ^=        \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));              \
      } inline constexpr(ENUM_TYPE) operator|(ENUM_TYPE a, ENUM_TYPE b) {      \
        return static_cast<ENUM_TYPE>(                                         \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(a)                \
            | static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));            \
      } inline constexpr(ENUM_TYPE) operator&(ENUM_TYPE a, ENUM_TYPE b) {      \
        return static_cast<ENUM_TYPE>(                                         \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(a)                \
            & static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));            \
      } inline constexpr(ENUM_TYPE) operator^(ENUM_TYPE a, ENUM_TYPE b) {      \
        return static_cast<ENUM_TYPE>(                                         \
            static_cast<std::underlying_type_t<(ENUM_TYPE)>>(a)                \
            ^ static_cast<std::underlying_type_t<(ENUM_TYPE)>>(b));            \
      } inline constexpr(ENUM_TYPE) operator~(ENUM_TYPE a) {                   \
        return static_cast<ENUM_TYPE>(                                         \
            ~static_cast<std::underlying_type_t<(ENUM_TYPE)>>(a));             \
      })
