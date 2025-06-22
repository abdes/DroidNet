//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Resource.h>

namespace oxygen {

//=== Forward Declarations ===------------------------------------------------//

// Scene graph and world objects
namespace scene {
  class SceneNode;
} // namespace scene

// Reserved slots for future engine resource types
class EngineReserved0;
class EngineReserved1;
class EngineReserved2;
class EngineReserved3;
class EngineReserved4;
class EngineReserved5;
class EngineReserved6;
class EngineReserved7;
class EngineReserved8;
class EngineReserved9;

//=== Centralized Resource Type Registry ===----------------------------------//

//! Global resource type list for all objects requiring ResourceTable storage.
/*!
 This TypeList determines compile-time resource type IDs for all
 Resource-derived objects in the Oxygen engine. The order defines the resource
 type IDs:
 - EngineReserved0 gets ID 0
 - EngineReserved1 gets ID 1
 - EngineReserved2 gets ID 2
 - etc.
 - SceneNode gets ID 10

 ### Usage Pattern

 All Resource classes must use this list as their ResourceTypeList parameter:
 ```cpp
 class MyResource : public Resource<MyResource, ResourceTypeList> {
     // Resource implementation
 };
 ```

 ### Binary Compatibility Rules

 **CRITICAL**: To maintain binary compatibility across versions:
 - **Never reorder** existing types in this list
 - **Never remove** types from this list
 - **Only append** new resource types to the end
 - **Maximum 256** resource types supported

 ### Adding New Resource Types

 To add a new resource type:
 1. Forward declare the class above
 2. Add it to the END of ResourceTypeList below
 3. Update this documentation

 @warning Changing the order of existing types will break binary compatibility!
 @see Resource, GetResourceTypeId, TypeList
*/
using ResourceTypeList = TypeList<
  // Reserved engine resource slots (0-9)
  EngineReserved0, // ID 0 - Reserved for future use
  EngineReserved1, // ID 1 - Reserved for future use
  EngineReserved2, // ID 2 - Reserved for future use
  EngineReserved3, // ID 3 - Reserved for future use
  EngineReserved4, // ID 4 - Reserved for future use
  EngineReserved5, // ID 5 - Reserved for future use
  EngineReserved6, // ID 6 - Reserved for future use
  EngineReserved7, // ID 7 - Reserved for future use
  EngineReserved8, // ID 8 - Reserved for future use
  EngineReserved9, // ID 9 - Reserved for future use

  // Core scene graph objects
  scene::SceneNode // ID 10 - Scene hierarchy nodes

  // Add new resource types here at the end only
  // DO NOT add non-pooled components here!
  // Only classes that inherit from Resource should be in this list
  >;

} // namespace oxygen
