//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// use this inside a class declaration to make it non-copyable
#define OXYGEN_MAKE_NON_COPYABLE(Type) \
    Type(const Type&) = delete;        \
    auto operator=(const Type&)->Type& = delete;

// use this inside a class declaration to make it non-moveable
// NOLINTBEGIN
#define OXYGEN_MAKE_NON_MOVEABLE(Type) \
    Type(Type&&) = delete;             \
    auto operator=(Type&&)->Type& = delete;
// NOLINTEND

// use this inside a class declaration to declare default copy constructor and
// assignment operator
#define OXYGEN_DEFAULT_COPYABLE(Type) \
    Type(const Type&) = default;      \
    auto operator=(const Type&)->Type& = default;

// use this inside a class declaration to declare default move constructor and
// move assignment operator
// NOLINTBEGIN
#define OXYGEN_DEFAULT_MOVABLE(Type) \
    Type(Type&&) = default;          \
    auto operator=(Type&&)->Type& = default;
// NOLINTEND

// Make a bit flag
#define OXYGEN_FLAG(x) (1 << (x))

// NOLINTBEGIN
#define OXYGEN_DEFINE_FLAGS_OPERATORS(EnumType)                                                                       \
    inline EnumType operator|(EnumType lhs, EnumType rhs)                                                             \
    {                                                                                                                 \
        return static_cast<EnumType>(                                                                                 \
            static_cast<std::underlying_type_t<EnumType>>(lhs) | static_cast<std::underlying_type_t<EnumType>>(rhs)); \
    }                                                                                                                 \
    inline EnumType& operator|=(EnumType& lhs, EnumType rhs)                                                          \
    {                                                                                                                 \
        lhs = lhs | rhs;                                                                                              \
        return lhs;                                                                                                   \
    }                                                                                                                 \
    inline EnumType operator&(EnumType lhs, EnumType rhs)                                                             \
    {                                                                                                                 \
        return static_cast<EnumType>(                                                                                 \
            static_cast<std::underlying_type_t<EnumType>>(lhs) & static_cast<std::underlying_type_t<EnumType>>(rhs)); \
    }                                                                                                                 \
    inline EnumType& operator&=(EnumType& lhs, EnumType rhs)                                                          \
    {                                                                                                                 \
        lhs = lhs & rhs;                                                                                              \
        return lhs;                                                                                                   \
    }                                                                                                                 \
    inline EnumType operator^(EnumType lhs, EnumType rhs)                                                             \
    {                                                                                                                 \
        return static_cast<EnumType>(                                                                                 \
            static_cast<std::underlying_type_t<EnumType>>(lhs) ^ static_cast<std::underlying_type_t<EnumType>>(rhs)); \
    }                                                                                                                 \
    inline EnumType& operator^=(EnumType& lhs, EnumType rhs)                                                          \
    {                                                                                                                 \
        lhs = lhs ^ rhs;                                                                                              \
        return lhs;                                                                                                   \
    }                                                                                                                 \
    inline EnumType operator~(EnumType flag)                                                                          \
    {                                                                                                                 \
        return static_cast<EnumType>(                                                                                 \
            ~static_cast<std::underlying_type_t<EnumType>>(flag));                                                    \
    }
// NOLINTEND
