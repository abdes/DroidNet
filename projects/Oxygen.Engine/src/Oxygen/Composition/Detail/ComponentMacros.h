//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if defined(DOXYGEN_DOCUMENTATION_BUILD)
//! @def OXYGEN_COMPONENT
//! Declares a class as an Oxygen Engine component type.
/*!
 Declares all required type information and registration hooks for a class to be
 used as a component in the Oxygen Engine composition system. This macro must be
 placed inside the declaration of a class derived from `oxygen::Component`.

 @param type The class name (required).

 ### Example Usage
 ```cpp
 class MyComponent : public oxygen::Component {
   OXYGEN_COMPONENT(MyComponent)
   // ...
 };
 ```

 ### Generated Code
 - Declares a protected default constructor
 - Declares a friend relationship with `oxygen::Composition`
 - Registers the type with the Oxygen type system (TypeId, TypeName)
 - Provides static type information accessors

 @see OXYGEN_POOLED, OXYGEN_COMPONENT_REQUIRES, oxygen::Component
*/
#else // DOXYGEN_DOCUMENTATION_BUILD
#  define OXYGEN_COMPONENT(type)                                               \
  public:                                                                      \
    friend class oxygen::Composition;                                          \
    OXYGEN_TYPED(type)                                                         \
  private:
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(DOXYGEN_DOCUMENTATION_BUILD)
//! @def OXYGEN_POOLED
//! Declares a component as a pooled component, associated with a resource type
//! list.
/*!
 Marks a component class as a pooled component, enabling it to be managed by a
 component pool for efficient storage and handle-based access. This macro must
 be placed inside the declaration of a class derived from `oxygen::Component`.

 @param type The class name (required).
 @param type_list The resource type list (required),e.g.,
 `oxygen::MyResourceTypeList`, that defines the global set of pooled component
 types.

 ### Example Usage
 ```cpp
 class MyPooledComponent : public oxygen::Component {
   OXYGEN_COMPONENT(MyPooledComponent)
   OXYGEN_POOLED(MyPooledComponent, MyResourceTypeList)
   // ...
 };
 ```

 ### Generated Code
 - Declares `static constexpr bool is_pooled = true;` for compile-time pooled
   detection
 - Declares `using ResourceTypeList = type_list;` for resource type list
   association

 @note The type list must be defined and registered elsewhere in the codebase.
 @see OXYGEN_COMPONENT, oxygen::ComponentPoolRegistry
*/
#else // DOXYGEN_DOCUMENTATION_BUILD
#  define OXYGEN_POOLED_COMPONENT(type, type_list)                             \
    OXYGEN_COMPONENT(type)                                                     \
  public:                                                                      \
    static constexpr bool is_pooled = true;                                    \
    using ResourceTypeList = type_list;                                        \
    static constexpr auto GetResourceType() noexcept                           \
      -> oxygen::ResourceHandle::ResourceTypeT                                 \
    {                                                                          \
      constexpr auto rt = oxygen::IndexOf<type, ResourceTypeList>::value;      \
      static_assert(rt <= oxygen::ResourceHandle::kResourceTypeMax,            \
        "Too many resource types for ResourceHandle::ResourceTypeT!");         \
      return static_cast<oxygen::ResourceHandle::ResourceTypeT>(rt);           \
    }                                                                          \
                                                                               \
  private:
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(DOXYGEN_DOCUMENTATION_BUILD)
//! @def OXYGEN_COMPONENT_REQUIRES(...)
//! Declares required Component dependencies that must exist before this
//! Component can be created.
/*!
 Declares that this component type requires one or more other component types to
 be present in the same composition before it can be created or used. This macro
 generates all necessary static and virtual methods for dependency introspection
 and validation.

 @param ... List of component class names that this component depends on. Pass
            one or more types, separated by commas. If no arguments are given,
            the component has no dependencies.

 ### Example Usage
 ```cpp
 class DependentComponent : public oxygen::Component {
   OXYGEN_COMPONENT(DependentComponent)
   OXYGEN_COMPONENT_REQUIRES(FirstDependency, SecondDependency)
   // ...
 };
 ```

 ### Generated Code
 - Static array of dependency TypeIds
 - Static accessor: `ClassDependencies()` returns the dependency list
 - Virtual overrides: `HasDependencies()` and `Dependencies()`

 @note Dependencies are validated at runtime when the component is created
       through `Composition::AddComponent`. If any required dependency is
       missing, an exception is thrown.
 @see oxygen::Component, oxygen::Composition, OXYGEN_COMPONENT
 @see https://www.scs.stanford.edu/~dm/blog/va-opt.html for macro expansion
*/
#else // DOXYGEN_DOCUMENTATION_BUILD
#  define OXYGEN_COMPONENT_REQUIRES(...)                                       \
    OXYGEN_COMPONENT_REQUIRES_WARN_(__VA_ARGS__) // NOLINT(*-avoid-c-arrays)
#  define OXYGEN_COMPONENT_REQUIRES_WARN_(...)                                 \
  private:                                                                     \
    inline static const oxygen::TypeId dependencies[]                          \
      = { __VA_OPT__(OXYGEN_EXPAND(FOR_EACH_HELPER(__VA_ARGS__))) };           \
                                                                               \
  public:                                                                      \
    static constexpr auto ClassDependencies()                                  \
      -> std::span<const oxygen::TypeId>                                       \
    {                                                                          \
      return { dependencies };                                                 \
    }                                                                          \
    auto HasDependencies() const noexcept -> bool override { return true; }    \
    auto Dependencies() const noexcept -> std::span<const oxygen::TypeId>      \
      override                                                                 \
    {                                                                          \
      return { dependencies };                                                 \
    }                                                                          \
                                                                               \
  private:

#  define FOR_EACH_HELPER(a1, ...)                                             \
    a1::ClassTypeId(), __VA_OPT__(FOR_EACH_AGAIN PARENS(__VA_ARGS__))
#  define FOR_EACH_AGAIN() FOR_EACH_HELPER

#  define PARENS ()

#  define OXYGEN_EXPAND(...)                                                   \
    OXYGEN_EXPAND4(OXYGEN_EXPAND4(OXYGEN_EXPAND4(OXYGEN_EXPAND4(__VA_ARGS__))))
#  define OXYGEN_EXPAND4(...)                                                  \
    OXYGEN_EXPAND3(OXYGEN_EXPAND3(OXYGEN_EXPAND3(OXYGEN_EXPAND3(__VA_ARGS__))))
#  define OXYGEN_EXPAND3(...)                                                  \
    OXYGEN_EXPAND2(OXYGEN_EXPAND2(OXYGEN_EXPAND2(OXYGEN_EXPAND2(__VA_ARGS__))))
#  define OXYGEN_EXPAND2(...)                                                  \
    OXYGEN_EXPAND1(OXYGEN_EXPAND1(OXYGEN_EXPAND1(OXYGEN_EXPAND1(__VA_ARGS__))))
#  define OXYGEN_EXPAND1(...) __VA_ARGS__
#endif // !defined(DOXYGEN_DOCUMENTATION_BUILD)
