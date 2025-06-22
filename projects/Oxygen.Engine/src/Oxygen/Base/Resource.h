//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ResourceHandle.h>
#include <type_traits>

namespace oxygen {

//=== Compile-Time Resource Type System ===-----------------------------------//

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

 @see GetResourceTypeId for usage in the resource type system
*/
template <typename T, typename List> struct IndexOf;

template <typename T, typename... Ts>
struct IndexOf<T, TypeList<T, Ts...>> : std::integral_constant<std::size_t, 0> {
};

template <typename T, typename U, typename... Ts>
struct IndexOf<T, TypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + IndexOf<T, TypeList<Ts...>>::value> { };

/*!
 Get compile-time unique resource type ID for any registered type.

 Returns the zero-based index of type T within ResourceTypeList, providing zero
 runtime overhead type resolution through template metaprogramming.

 @tparam T The resource type to get ID for (must exist in ResourceTypeList)
 @tparam ResourceTypeList Centralized TypeList containing all valid resource
 types
 @return Compile-time constant resource type ID (0-255)

 @see Resource class documentation for detailed usage patterns, performance
      characteristics, and binary compatibility requirements
*/
template <typename T, typename ResourceTypeList>
constexpr auto GetResourceTypeId() noexcept -> ResourceHandle::ResourceTypeT
{
  static_assert(IndexOf<T, ResourceTypeList>::value < 256,
    "Too many resource types for ResourceHandle::ResourceTypeT!");
  return static_cast<ResourceHandle::ResourceTypeT>(
    IndexOf<T, ResourceTypeList>::value);
}

/*!
 Base class for objects that require handle-based access into a ResourceTable
 where the objects are stored.

 Resources use high-performance, cache-friendly storage for frequently accessed
 objects using compile-time type identification and contiguous memory layout.
 They are designed for scenarios where O(1) access, automatic handle validation,
 and memory defragmentation are critical for performance.

 ### When to Use Resource

 Inherit from Resource when your object needs:
 - **High-frequency access** (transforms, scene nodes, pooled components)
 - **Cache-friendly storage** with contiguous memory layout
 - **Handle-based indirection** with automatic validation and invalidation
 - **Built-in defragmentation** to maintain cache locality over time
 - **Cross-module consistency** with compile-time type safety

 ### When NOT to Use Resource

 Do NOT inherit from Resource for:
 - **Low-frequency objects** (settings, managers, one-off instances)
 - **RAII wrappers** around external APIs
 - **Simple data containers** without complex lifecycle needs

 ### Architecture Integration

 Resources integrate with the broader Oxygen engine architecture:
 - **Scene nodes**: Use ResourceTable for spatial hierarchy management
 - **Pooled components**: Stored in ResourceTable for high-frequency access
 - **Graphics resources**: Textures, buffers, pipelines with GPU correlation
 - **Entity-component systems**: Handle-based correlation between entities and
   components

 ### Usage Example

 ```cpp
 // In ResourceTypes.h - centralized type registry
 using ResourceTypeList = TypeList<SceneNode, TransformComponent, Texture>;

 // Scene node as a Resource
 class SceneNode : public Resource<SceneNode, ResourceTypeList> {
 public:
     explicit SceneNode(const std::string& name);
     // ... scene node functionality
 };

 // Pooled component as both Component and Resource
 class TransformComponent : public Component,
                           public Resource<TransformComponent, ResourceTypeList>
 { OXYGEN_COMPONENT(TransformComponent) public: void SetPosition(const Vec3&
 pos);
     // ... transform functionality
 };
 ```

 ### Performance Characteristics

 The use of ResourceTable for storage and ResourceHandle for indirection
 provides the following performance characteristics:
 - **Access Time**: O(1) with generation counter validation
 - **Memory Layout**: Contiguous storage, Pooled allocation

 The use of compile-time resource type IDs provides the following benefits:
 - **Resource Type Resolution**: Zero runtime overhead

 @tparam ResourceT The derived resource type (CRTP pattern - must be the
 inheriting class)
 @tparam ResourceTypeList Centralized TypeList containing ALL resource types
 (order matters!)
 @tparam HandleT Handle type for indirection (defaults to ResourceHandle)

 @warning ResourceTypeList order determines compile-time type IDs. Never reorder
 existing types - only append new types to maintain binary compatibility!
 @warning Maximum 256 resource types supported due to
 ResourceHandle::ResourceTypeT being uint8_t

 @see ResourceTable, ResourceHandle, GetResourceTypeId
*/
template <typename ResourceT, typename ResourceTypeList,
  typename HandleT = ResourceHandle>
  requires(std::is_base_of_v<ResourceHandle, HandleT>)
class Resource {
public:
  //! Compile-time allocated resource type ID
  static constexpr ResourceHandle::ResourceTypeT kResourceType
    = GetResourceTypeId<ResourceT, ResourceTypeList>();

  constexpr explicit Resource(HandleT handle)
    : handle_(std::move(handle))
  {
    assert(handle_.ResourceType() == kResourceType);
  }

  virtual ~Resource() = default;

  OXYGEN_DEFAULT_COPYABLE(Resource)
  OXYGEN_DEFAULT_MOVABLE(Resource)

  [[nodiscard]] constexpr auto GetHandle() const noexcept -> const HandleT&
  {
    return handle_;
  }

  [[nodiscard]] constexpr auto GetResourceType() const noexcept
    -> ResourceHandle::ResourceTypeT
  {
    return kResourceType;
  }

  [[nodiscard]] virtual auto IsValid() const noexcept -> bool
  {
    return handle_.IsValid();
  }

protected:
  constexpr Resource()
    : handle_(HandleT::kInvalidIndex, kResourceType)
  {
  }

  constexpr void Invalidate() { handle_.Invalidate(); }

private:
  HandleT handle_;
};

} // namespace oxygen
