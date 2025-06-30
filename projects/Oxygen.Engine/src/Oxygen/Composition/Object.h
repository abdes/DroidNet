//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

//! Abstract base class for all type-aware objects in the Oxygen Engine.
/*!
 Provides a uniform interface for runtime type identification and reflection.
 All components, resources, and compositions derive from `Object` to enable
 type-safe queries and dynamic dispatch without relying on C++ RTTI.

 ### Key Features
 - **Type Identification**: Requires derived classes to implement `GetTypeId`,
   `GetTypeName`, and `GetTypeNamePretty` for runtime type queries.
 - **Copy/Move Support**: Supports copy and move semantics for flexible object
   management.

 ### Usage Patterns
 - Inherit from `Object` for any class that participates in the Oxygen type
   system.
 - Use the `OXYGEN_TYPED` macro to implement required type methods.

 @see OXYGEN_TYPED, Component, Composition
*/
class Object {
public:
  OXGN_COM_API Object();
  OXGN_COM_API virtual ~Object();

  OXYGEN_DEFAULT_COPYABLE(Object)
  OXYGEN_DEFAULT_MOVABLE(Object)

  [[nodiscard]] virtual auto GetTypeId() const noexcept -> TypeId = 0;
  [[nodiscard]] virtual auto GetTypeName() const noexcept -> std::string_view
    = 0;
  [[nodiscard]] virtual auto GetTypeNamePretty() const noexcept
    -> std::string_view
    = 0;
};

} // namespace oxygen
