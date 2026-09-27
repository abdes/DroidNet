//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::async {

// Per-sphere animation state (multiple spheres with different speeds)
struct SphereState {
  scene::SceneNode node;
  glm::vec3 orbit_center { 0.0F };
  // Base phases used for absolute-time evaluation (no per-frame drift)
  double base_angle { 0.0 };
  double speed { 0.0 }; // radians/sec
  double radius { 4.0 }; // orbit radius in world units
  double inclination { 0.5 }; // tilt of orbital plane (radians)
  double spin_speed { 0.0 }; // self-rotation speed (radians/sec)
  double base_spin_angle { 0.0 }; // initial spin phase
};

// Debug overlay tracking structures
struct FrameActionTracker {
  std::chrono::steady_clock::time_point frame_start_time;
  std::chrono::steady_clock::time_point frame_end_time;
  std::vector<std::pair<std::string, std::chrono::microseconds>> phase_timings;
  std::vector<std::string> frame_actions;
};

} // namespace oxygen::examples::async
