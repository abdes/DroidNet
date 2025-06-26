//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/Component.h>

namespace oxygen::composition::detail {

//! Type-erased interface for component pools
/*!
 Type-erased base class for all component pools, enabling generic management
 and access to pooled components regardless of their concrete type.

 ### Key Features

 - **Type Erasure**: Allows code to interact with component pools without
   knowing the specific component type at compile time.
 - **Virtual API**: Provides a uniform interface for allocation, deallocation,
   and lookup of components using base `Component` pointers and resource
   handles.
 - **Extensibility**: Designed for use by registries and systems that need to
   manage heterogeneous component pools.

 ### Usage Patterns

  - Used as a base for `ComponentPool<T>` implementations.
  - Enables storage of pools in containers and registries by pointer.

  @see oxygen::ComponentPool, oxygen::Component
*/
class ComponentPoolUntyped {
public:
  //! Virtual destructor for safe polymorphic deletion
  virtual ~ComponentPoolUntyped() = default;

  //! Get a const pointer to a component by handle (type-erased)
  /*! @param handle Resource handle for the component
      @return Const pointer to component, or nullptr if not found
      @note No ownership is transferred. */
  virtual auto GetUntyped(ResourceHandle handle) const noexcept
    -> const Component* = 0;

  //! Get a mutable pointer to a component by handle (type-erased)
  /*! @param handle Resource handle for the component
      @return Mutable pointer to component, or nullptr if not found
      @note No ownership is transferred. */
  virtual auto GetUntyped(ResourceHandle handle) noexcept -> Component* = 0;

  //! Allocate a component by moving from a base Component
  /*! @param src Component to move into the pool (must match pool type)
      @return Handle to the new component
      @throws std::invalid_argument if type does not match */
  virtual auto Allocate(Component&& src) -> ResourceHandle = 0;

  //! Allocate a component by moving from a unique_ptr<Component>
  /*! @param comp Unique pointer to component (must match pool type)
      @return Handle to the new component
      @throws std::invalid_argument if type does not match or pointer is null */
  virtual auto Allocate(std::unique_ptr<Component> comp) -> ResourceHandle = 0;

  //! Deallocate a component by handle
  /*! @param handle Resource handle for the component
      @return 1 if removed, 0 if not found */
  virtual auto Deallocate(ResourceHandle handle) noexcept -> size_t = 0;
};

} // namespace oxygen::composition::detail
