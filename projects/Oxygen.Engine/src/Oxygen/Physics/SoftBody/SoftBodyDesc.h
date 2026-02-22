//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Handles.h>

namespace oxygen::physics::softbody {

/*!
 Soft-body creation descriptor.

 Ownership contract:
 - `anchor_body_id` may bind soft-body behavior to an existing rigid body.
 - Soft-body aggregate owns deformable topology/state, not anchor body lifetime.
*/
struct SoftBodyDesc final {
  BodyId anchor_body_id { kInvalidBodyId };
  uint32_t cluster_count { 0U };
};

/*!
 Lightweight soft-body state snapshot.
*/
struct SoftBodyState final {
  Vec3 center_of_mass { 0.0F, 0.0F, 0.0F };
  bool sleeping { false };
};

/*!
 Runtime soft-body material/tuning parameters.
*/
struct SoftBodyMaterialParams final {
  float stiffness { 0.0F };
  float damping { 0.0F };
};

} // namespace oxygen::physics::softbody
