//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::events {

enum class PhysicsEventType : uint8_t {
  kContactBegin = 0,
  kContactEnd,
  kTriggerBegin,
  kTriggerEnd,
};

OXGN_PHYS_NDAPI auto to_string(PhysicsEventType value) noexcept
  -> std::string_view;

struct PhysicsEvent final {
  PhysicsEventType type { PhysicsEventType::kContactBegin };
  BodyId body_a { kInvalidBodyId };
  BodyId body_b { kInvalidBodyId };
  uint64_t user_data_a { 0 };
  uint64_t user_data_b { 0 };
  Vec3 contact_normal { oxygen::space::move::Up };
  Vec3 contact_position { 0.0F, 0.0F, 0.0F };
  float penetration_depth { 0.0F };
  Vec3 applied_impulse { 0.0F, 0.0F, 0.0F };
};

} // namespace oxygen::physics::events
