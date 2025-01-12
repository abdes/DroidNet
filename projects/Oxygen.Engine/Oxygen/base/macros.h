//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// use this inside a class declaration to make it non-copyable
#define OXYGEN_MAKE_NON_COPYABLE(Type) \
  Type(const Type&) = delete;          \
  auto operator=(const Type&)->Type& = delete;

// use this inside a class declaration to make it non-moveable
// NOLINTBEGIN
#define OXYGEN_MAKE_NON_MOVEABLE(Type) \
  Type(Type&&) = delete;               \
  auto operator=(Type&&)->Type& = delete;
// NOLINTEND

// use this inside a class declaration to declare default copy constructor and
// assignment operator
#define OXYGEN_DEFAULT_COPYABLE(Type) \
  Type(const Type&) = default;        \
  auto operator=(const Type&)->Type& = default;

// use this inside a class declaration to declare default move constructor and
// move assignment operator
// NOLINTBEGIN
#define OXYGEN_DEFAULT_MOVABLE(Type) \
  Type(Type&&) = default;            \
  auto operator=(Type&&)->Type& = default;
// NOLINTEND

// Make a bit flag
#define OXYGEN_FLAG(x) (1 << (x))

// NOLINTBEGIN
#define OXYGEN_DEFINE_FLAGS_OPERATORS(EnumType)                                                                 \
  inline EnumType operator|(EnumType lhs, EnumType rhs)                                                         \
  {                                                                                                             \
    return static_cast<EnumType>(                                                                               \
      static_cast<std::underlying_type_t<EnumType>>(lhs) | static_cast<std::underlying_type_t<EnumType>>(rhs)); \
  }                                                                                                             \
  inline EnumType& operator|=(EnumType& lhs, EnumType rhs)                                                      \
  {                                                                                                             \
    lhs = lhs | rhs;                                                                                            \
    return lhs;                                                                                                 \
  }                                                                                                             \
  inline EnumType operator&(EnumType lhs, EnumType rhs)                                                         \
  {                                                                                                             \
    return static_cast<EnumType>(                                                                               \
      static_cast<std::underlying_type_t<EnumType>>(lhs) & static_cast<std::underlying_type_t<EnumType>>(rhs)); \
  }                                                                                                             \
  inline EnumType& operator&=(EnumType& lhs, EnumType rhs)                                                      \
  {                                                                                                             \
    lhs = lhs & rhs;                                                                                            \
    return lhs;                                                                                                 \
  }                                                                                                             \
  inline EnumType operator^(EnumType lhs, EnumType rhs)                                                         \
  {                                                                                                             \
    return static_cast<EnumType>(                                                                               \
      static_cast<std::underlying_type_t<EnumType>>(lhs) ^ static_cast<std::underlying_type_t<EnumType>>(rhs)); \
  }                                                                                                             \
  inline EnumType& operator^=(EnumType& lhs, EnumType rhs)                                                      \
  {                                                                                                             \
    lhs = lhs ^ rhs;                                                                                            \
    return lhs;                                                                                                 \
  }                                                                                                             \
  inline EnumType operator~(EnumType flag)                                                                      \
  {                                                                                                             \
    return static_cast<EnumType>(                                                                               \
      ~static_cast<std::underlying_type_t<EnumType>>(flag));                                                    \
  }
// NOLINTEND

//! Adds the necessary declarations for a class as a typed component.
/*!
 \param arg_type The class name.

 Example:
 \code{.cpp}
   class MyComponent : public oxygen::Component {
     OXYGEN_COMPONENT(MyComponent)
   public:
     // Component interface...
   };
 \endcode

 Generated code:
 - Protected default constructor
 - Friend declaration for `Composition`
 - Type system registration (`TypeId`, `TypeName`)
 - Static type information accessors

 \note Components must be instantiated through `Composition::AddComponent`.
*/
#define OXYGEN_COMPONENT(arg_type)  \
 protected:                         \
  friend class oxygen::Composition; \
  arg_type() = default;             \
                                    \
  OXYGEN_TYPED(arg_type)

// https://www.scs.stanford.edu/~dm/blog/va-opt.html

//! Declares required Component dependencies that must exist before this
//! Component can be created.
/*!
 \param ... Calls names of components that this Component depends on.

 Example:
 \code{.cpp}
   class DependentComponent : public oxygen::Component {
     OXYGEN_COMPONENT(DependentComponent)
     OXYGEN_COMPONENT_REQUIRES(FirstDependency, SecondDependency)
   public:
     //Component interface...
   };
 \endcode

 Generated code:
 - Static array of dependency TypeIds
 - ClassDependencies() accessor for dependency list
 - HasDependencies() override returning true
 - Dependencies() override returning dependency list

 \note Dependencies are validated when Component is created through
 Composition::AddComponent
 \see https://www.scs.stanford.edu/~dm/blog/va-opt.html
*/
#define OXYGEN_COMPONENT_REQUIRES(...)                                                                      \
 private:                                                                                                   \
  inline static const oxygen::TypeId dependencies[] = {                                                     \
    __VA_OPT__(OXYGEN_EXPAND(FOR_EACH_HELPER(__VA_ARGS__)))                                                 \
  };                                                                                                        \
                                                                                                            \
 public:                                                                                                    \
  static constexpr auto ClassDependencies() -> std::span<const oxygen::TypeId> { return { dependencies }; } \
  constexpr bool HasDependencies() const override { return true; }                                          \
  constexpr std::span<const oxygen::TypeId> Dependencies() const override { return { dependencies }; }      \
                                                                                                            \
 private:

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)
#define FOR_EACH_HELPER(a1, ...) a1::ClassTypeId(), __VA_OPT__(FOR_EACH_AGAIN PARENS(__VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER

#define PARENS ()

#define OXYGEN_EXPAND(...) OXYGEN_EXPAND4(OXYGEN_EXPAND4(OXYGEN_EXPAND4(OXYGEN_EXPAND4(__VA_ARGS__))))
#define OXYGEN_EXPAND4(...) OXYGEN_EXPAND3(OXYGEN_EXPAND3(OXYGEN_EXPAND3(OXYGEN_EXPAND3(__VA_ARGS__))))
#define OXYGEN_EXPAND3(...) OXYGEN_EXPAND2(OXYGEN_EXPAND2(OXYGEN_EXPAND2(OXYGEN_EXPAND2(__VA_ARGS__))))
#define OXYGEN_EXPAND2(...) OXYGEN_EXPAND1(OXYGEN_EXPAND1(OXYGEN_EXPAND1(OXYGEN_EXPAND1(__VA_ARGS__))))
#define OXYGEN_EXPAND1(...) __VA_ARGS__
#endif // !defined(DOXYGEN_DOCUMENTATION_BUILD)
