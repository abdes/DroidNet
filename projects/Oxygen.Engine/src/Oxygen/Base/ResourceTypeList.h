//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/ResourceHandle.h>

/*!
 @file ResourceTypeList.h

 ## Compile Time Resource Type System

 Defines the compile-time type list and type-indexing utilities for Oxygen
 Engine resource and pooled component systems. This file provides the `TypeList`
 and `IndexOf` templates, as well as the `GetResourceTypeId` function for
 mapping resource types to unique, stable, compile-time IDs.

 ### Usage and Binary Compatibility Requirements

 - All resource types (any type derived from Resource) and pooled ovject types
   (any type that uses ResourceTable for storage) must be listed in a single
   `TypeList` (e.g., `ResourceTypeList`).
 - The order of types in the list determines their type ID. **Never reorder
   existing types**; only append new types to the end to maintain binary
   compatibility across builds and modules.
 - Forward declare all resource/component types before defining the type list to
   avoid circular dependencies and enable use in headers.
 - The type list must be visible to all code that needs to resolve resource type
   IDs at compile time (e.g., pools, handles, registries).

 ### Example Usage

 ```cpp
 // Forward declare all resource/component types
 class TransformComponent;
 class SceneNode;
 class Texture;

 // Define the type list in a central header
 using ResourceTypeList = oxygen::TypeList<
     TransformComponent,
     SceneNode,
     Texture
 >;

 // Pooled component (uses ResourceTable for storage), but is not a Resource.
 // It can still be used in the same TypeList for type ID resolution.
 class TransformComponent : public oxygen::Component {
   OXYGEN_COMPONENT(TransformComponent, true) // Mark as pooled
   // ...
 };

 // Resource type, holds a handle to an object stored in a ResourceTable.
 class SceneNode : public oxygen::Resource<SceneNode, ResourceTypeList> {
   // ...
 };

 // Get a type ID at compile time
 constexpr auto id = oxygen::GetResourceTypeId<SceneNode, ResourceTypeList>();
 ```

 @warning Changing the order of types in the type list will break binary
          compatibility for all handles and pools. Only append new types.
*/

namespace oxygen {

/*!
 Template metaprogramming-based resource type system. Provides compile-time
 unique resource type IDs without RTTI or runtime overhead.
*/
template <typename... Ts> struct TypeList { };

/*!
 Template metaprogramming helper to find the index of a type in a TypeList.

 Recursively searches through the TypeList at compile time and returns the
 zero-based index where the type is found. Generates a compile error if the type
 is not found, providing type safety.

 @tparam T The type to search for
 @tparam List The TypeList to search in
*/
template <typename T, typename List> struct IndexOf;

template <typename T, typename... Ts>
struct IndexOf<T, TypeList<T, Ts...>> : std::integral_constant<std::size_t, 0> {
};

template <typename T, typename U, typename... Ts>
struct IndexOf<T, TypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + IndexOf<T, TypeList<Ts...>>::value> { };

} // namespace oxygen
