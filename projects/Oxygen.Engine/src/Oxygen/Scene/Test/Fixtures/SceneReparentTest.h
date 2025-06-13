//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "./SceneTest.h"

namespace oxygen::scene::testing {

class SceneReparentTest : public SceneTest {
protected:
  // Helper: Create test vectors and quaternions
  static constexpr auto MakeVec3(const float x, const float y, const float z)
    -> scene::detail::TransformComponent::Vec3
  {
    return scene::detail::TransformComponent::Vec3 { x, y, z };
  }

  static auto MakeQuat(const float w, const float x, const float y,
    const float z) -> scene::detail::TransformComponent::Quat
  {
    return scene::detail::TransformComponent::Quat { w, x, y, z };
  }

  static auto QuatFromEuler(const float x_deg, const float y_deg,
    const float z_deg) -> scene::detail::TransformComponent::Quat
  {
    return scene::detail::TransformComponent::Quat { glm::radians(
      scene::detail::TransformComponent::Vec3 { x_deg, y_deg, z_deg }) };
  }

  // Helper: Verify vectors are approximately equal
  static void ExpectVec3Near(
    const scene::detail::TransformComponent::Vec3& actual,
    const scene::detail::TransformComponent::Vec3& expected,
    const float tolerance = 1e-5f)
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // Helper: Verify quaternions are approximately equal
  static void ExpectQuatNear(
    const scene::detail::TransformComponent::Quat& actual,
    const scene::detail::TransformComponent::Quat& expected,
    const float tolerance = 1e-5f)
  {
    EXPECT_NEAR(actual.w, expected.w, tolerance);
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
};

//=== Categorized Reparent Test Fixtures ===--------------------------------//

//! Base class for basic reparenting functionality tests.
class SceneReparentBasicTest : public SceneReparentTest { };

//! Base class for deep reparenting tests.
class SceneReparentErrorTest : public SceneReparentTest { };

//! Base class for deep reparenting tests.
class SceneReparentDeathTest : public SceneReparentTest { };

//! Base class for reparenting performance tests.
class SceneReparentEdgeTest : public SceneReparentTest { };

} // namespace oxygen::scene::testing
