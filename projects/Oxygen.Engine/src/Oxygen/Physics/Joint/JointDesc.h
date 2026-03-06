//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::joint {

enum class JointType : uint8_t {
  kFixed = 0,
  kDistance,
  kHinge,
  kSlider,
  kSpherical,
};

OXGN_PHYS_NDAPI auto to_string(JointType value) noexcept -> std::string_view;

struct JointDesc final {
  JointType type { JointType::kFixed };
  BodyId body_a { kInvalidBodyId };
  BodyId body_b { kInvalidBodyId };
  std::span<const uint8_t> constraint_settings_blob {};
  Vec3 anchor_a { 0.0F, 0.0F, 0.0F };
  Vec3 anchor_b { 0.0F, 0.0F, 0.0F };
  bool collide_connected { false };
  float stiffness { 0.0F };
  float damping { 0.0F };
};

} // namespace oxygen::physics::joint
