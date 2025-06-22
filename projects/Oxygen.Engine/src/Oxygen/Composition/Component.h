//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
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
  OXGN_COM_API Component() = default;
  OXGN_COM_API virtual auto UpdateDependencies(
    const std::function<Component&(TypeId)>& /*get_component*/) -> void
  {
    // Default: do nothing
  }

private:
  friend class Composition;
  static constexpr auto ClassDependencies() -> std::span<const TypeId>
  {
    return {};
  }
  [[nodiscard]] virtual auto HasDependencies() const -> bool { return false; }
  [[nodiscard]] virtual auto Dependencies() const -> std::span<const TypeId>
  {
    return ClassDependencies();
  }
};

// Non-member swap (move semantics implementation in derived classes)
inline auto swap(Component& /*lhs*/, Component& /*rhs*/) noexcept -> void { }

//! Specifies the requirements on a type to be considered as a Component.
template <typename T>
concept IsComponent = std::is_base_of_v<Component, T>;

} // namespace oxygen
