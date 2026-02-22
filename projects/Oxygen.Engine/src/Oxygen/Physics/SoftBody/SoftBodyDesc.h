//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::softbody {

enum class SoftBodyTetherMode : uint8_t {
  kNone,
  kEuclidean,
  kGeodesic,
};

OXGN_PHYS_NDAPI auto to_string(SoftBodyTetherMode value) noexcept
  -> std::string_view;

/*!
 Runtime soft-body material/tuning parameters.
*/
struct SoftBodyMaterialParams final {
  float stiffness { 0.0F };
  float damping { 0.0F };
  float edge_compliance { 0.0F };
  float shear_compliance { 0.0F };
  float bend_compliance { std::numeric_limits<float>::max() };
  SoftBodyTetherMode tether_mode { SoftBodyTetherMode::kNone };
  float tether_max_distance_multiplier { 1.0F };
};

/*!
 Soft-body creation descriptor.

 Ownership contract:
 - `anchor_body_id` requests backend-supported soft-body anchoring to an
   existing body.
 - Backends that do not support anchor-to-body constraints return
   `PhysicsError::kNotImplemented`.
 - Soft-body aggregate owns deformable topology/state, not anchor body lifetime.
*/
struct SoftBodyDesc final {
  BodyId anchor_body_id { kInvalidBodyId };
  uint32_t cluster_count { 0U };
  SoftBodyMaterialParams material_params {};
};

/*!
 Lightweight soft-body state snapshot.
*/
struct SoftBodyState final {
  Vec3 center_of_mass { 0.0F, 0.0F, 0.0F };
  bool sleeping { false };
};

} // namespace oxygen::physics::softbody
