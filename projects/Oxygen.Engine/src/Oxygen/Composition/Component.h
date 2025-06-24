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
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

class ComponentError final : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Composition;

class Component : public virtual Object {
public:
  OXGN_COM_API ~Component() override = default;

  // All components should implement proper copy and move semantics to handle
  // copying and moving as appropriate.
  OXYGEN_DEFAULT_COPYABLE(Component)
  OXYGEN_DEFAULT_MOVABLE(Component)

  //== Default Component Dependencies ===-------------------------------------//

  [[nodiscard]] virtual auto HasDependencies() const noexcept -> bool
  {
    return false;
  }
  [[nodiscard]] virtual auto Dependencies() const noexcept
    -> std::span<const TypeId>
  {
    return {};
  }

  [[nodiscard]] virtual auto IsCloneable() const noexcept -> bool
  {
    return false;
  }

  //! Create a clone of the component.
  /*!
   \note The clone will not have any dependencies updated until a call to its
   UpdateDependencies method is made.
  */
  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Component>
  {
    throw ComponentError("Component is not cloneable");
  }

protected:
  friend class Composition;
  OXGN_COM_API Component() = default;
  OXGN_COM_API virtual auto UpdateDependencies(
    const std::function<Component&(TypeId)>& /*get_component*/) noexcept -> void
  {
    // Default: do nothing
  }
};

// Non-member swap (move semantics implementation in derived classes)
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
#include <Oxygen/Composition/Detail/ComponentMacros.h>
