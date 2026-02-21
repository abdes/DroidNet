//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/CollisionLayers.h>

namespace oxygen::physics {

//! Broad phase layer identifier for fast collision culling.
using BroadPhaseLayer = uint8_t;

//! Interface for defining collision filtering rules.
/*!
 This interface abstracts the two-tier collision filtering system used by
 modern physics engines like Jolt. It maps detailed collision layers to
 broader phase layers for fast culling, and defines the collision matrix
 between them.
*/
class ICollisionFilter {
public:
  ICollisionFilter() = default;
  virtual ~ICollisionFilter() = default;

  OXYGEN_MAKE_NON_COPYABLE(ICollisionFilter)
  OXYGEN_MAKE_NON_MOVABLE(ICollisionFilter)

  //! Maps a detailed collision layer to a broader phase layer.
  virtual auto GetBroadPhaseLayer(CollisionLayer layer) const noexcept
    -> BroadPhaseLayer
    = 0;

  //! Determines if two detailed layers should collide.
  virtual auto ShouldCollide(
    CollisionLayer layer1, CollisionLayer layer2) const noexcept -> bool
    = 0;

  //! Determines if a detailed layer should collide with a broad phase layer.
  virtual auto ShouldCollide(
    CollisionLayer layer1, BroadPhaseLayer bp_layer) const noexcept -> bool
    = 0;
};

} // namespace oxygen::physics
