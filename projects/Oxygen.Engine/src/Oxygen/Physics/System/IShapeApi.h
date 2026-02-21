//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Shape/ShapeDesc.h>

namespace oxygen::physics::system {

//! Reusable collision-shape domain API.
/*!
 Responsibilities now:
 - Create and destroy reusable shape resources.
 - Share shape resources across bodies and areas.
 - Enforce shape lifetime rules for attached shape instances.

 ### Near Future

 - Support compound-shape authoring and shape mutation with backend-safe
   rebuild semantics.
*/
class IShapeApi {
public:
  IShapeApi() = default;
  virtual ~IShapeApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IShapeApi)
  OXYGEN_MAKE_NON_MOVABLE(IShapeApi)

  virtual auto CreateShape(const shape::ShapeDesc& desc)
    -> PhysicsResult<ShapeId>
    = 0;

  /*!
   Destroy contract:
   - Returns `kNotFound` equivalent (`kInvalidArgument` or backend-specific
     not-found code) when `shape_id` does not exist.
   - Returns `kAlreadyExists` when the shape is still attached to any body or
     area shape instance.
   - Does not auto-detach attached instances.
  */
  virtual auto DestroyShape(ShapeId shape_id) -> PhysicsResult<void> = 0;
};

} // namespace oxygen::physics::system
