//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

//! Exception type for all component-related errors in the composition system.
class ComponentError final : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Composition;

//! Base class for all Oxygen Engine components.
/*!
 The `Component` class is the root of all data-oriented parts that can be
 composed into a `Composition`. Components can be either unique (local to a
 composition) or pooled (managed by a global pool for memory efficiency).

 ### Key Features
 - **Copy, Move, and Clone**: Supports copy, move, and (optionally) clone
   semantics for deep copying.
 - **Dependency Management**: Supports compile-time and runtime dependency
   declaration and validation.
 - **Dependency Resolution**: Provides a virtual `UpdateDependencies` method
   for resolving dependencies after construction.
 - **Macro Integration**: Used in conjunction with macros such as
   `OXYGEN_COMPONENT`, `OXYGEN_POOLED_COMPONENT`, and
   `OXYGEN_COMPONENT_REQUIRES` for type registration and dependency management.

 ### Usage Patterns
 - Inherit from `Component` for all custom components.
 - Use the appropriate macro for registration.
 - Optionally override `Clone()` and `IsCloneable()` for deep copy support.
 - Optionally override `UpdateDependencies()` to cache pointers/handles to
   required components.

 @see Composition, OXYGEN_COMPONENT, OXYGEN_POOLED_COMPONENT,
   OXYGEN_COMPONENT_REQUIRES
*/
class Component : public virtual Object {
public:
  OXGN_COM_API ~Component() override = default;

  // All components should implement proper copy and move semantics to handle
  // copying and moving as appropriate.
  OXYGEN_DEFAULT_COPYABLE(Component)
  OXYGEN_DEFAULT_MOVABLE(Component)

  //== Component Dependencies ===---------------------------------------------//

  [[nodiscard]] virtual auto HasDependencies() const noexcept -> bool
  {
    return false;
  }
  [[nodiscard]] virtual auto Dependencies() const noexcept
    -> std::span<const TypeId>
  {
    return {};
  }

  //== Cloning Behavior ===---------------------------------------------------//

  //! Indicates whether this component supports deep cloning.
  /*!
   @return `true` if the component implements `Clone()`, otherwise `false`.
   @see Clone
  */
  [[nodiscard]] virtual auto IsCloneable() const noexcept -> bool
  {
    return false;
  }

  //! Create a clone of the component.
  /*!
   @note The clone will not have any dependencies updated until a call to its
   UpdateDependencies method is made.
  */
  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Component>
  {
    throw ComponentError("Component is not cloneable");
  }

protected:
  friend class Composition;
  OXGN_COM_API Component() = default; // Created only via Composition

  //! Resolves and caches pointers or handles to required component
  //! dependencies.
  /*!
   Called after all required dependencies have been constructed and added to the
   composition. Override this method in your component to cache references to
   dependencies using the provided lookup function.

   @param get_component A function that returns a reference to a component by
     its TypeId. Use this to retrieve and store pointers or handles to required
     dependencies.

   @note The default implementation does nothing.

   @see OXYGEN_COMPONENT_REQUIRES, HasDependencies, Dependencies
  */
  OXGN_COM_API virtual auto UpdateDependencies(
    [[maybe_unused]] const std::function<Component&(TypeId)>&
      get_component) noexcept -> void
  {
    // Default: do nothing
  }
};

//! Non-member swap (move semantics implementation in derived classes)
inline auto swap(Component& /*lhs*/, Component& /*rhs*/) noexcept -> void { }

//! Specifies the requirements on a type to be considered as a Component.
template <typename T>
concept IsComponent = std::is_base_of_v<Component, T>;

//! Concept to check if a Component is a pooled component.
template <typename T>
concept IsPooledComponent = IsComponent<T> && requires {
  { T::is_pooled } -> std::convertible_to<bool>;
} && T::is_pooled;

//! Concept to check if a Component has dependencies.
template <typename T>
concept IsComponentWithDependencies = IsComponent<T> && requires {
  { T::ClassDependencies() };
};

} // namespace oxygen

// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Composition/ComponentMacros.h>
