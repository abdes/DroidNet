//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/matrix_transform.hpp>

#include <EditorModule/EditorCameraPlacement.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using namespace oxygen::interop::module::viewport;

namespace {

constexpr float kTolerance = 1.0e-4F;

//! A rotated, translated and non-uniformly scaled parent.
auto MakeParent() -> ParentFrame {
  const auto rotation = glm::angleAxis(glm::radians(35.0F),
    glm::normalize(glm::vec3 { 0.3F, -0.4F, 0.85F }));
  const auto world = glm::translate(glm::mat4 { 1.0F }, { 4.0F, -2.0F, 7.5F })
    * glm::mat4_cast(rotation) * glm::scale(glm::mat4 { 1.0F }, { 2.0F, 0.5F, 3.0F });
  return { .world_matrix = world, .world_rotation = rotation };
}

auto MakeCameraPlacement() -> CameraPlacement {
  return {
    .position = { 1.5F, -3.0F, 2.25F },
    .rotation = glm::normalize(glm::angleAxis(glm::radians(-70.0F),
      glm::normalize(glm::vec3 { 1.0F, 0.2F, -0.1F }))),
  };
}

void AssertSame(const glm::vec3& expected, const glm::vec3& actual) {
  Assert::AreEqual(expected.x, actual.x, kTolerance);
  Assert::AreEqual(expected.y, actual.y, kTolerance);
  Assert::AreEqual(expected.z, actual.z, kTolerance);
}

} // namespace

namespace InteropTests {

[TestClass]
public ref class EditorCameraPlacementTests {
public:
  [TestMethod]
  void RootPlacementIsItsOwnWorldPlacement() {
    const auto local = MakeCameraPlacement();
    const auto world = ToWorldPlacement(local, {});
    AssertSame(local.position, world.position);
    Assert::IsTrue(IsSamePlacement(local, world));
    Assert::IsTrue(IsSamePlacement(local, ToParentPlacement(world, {})));
  }

  [TestMethod]
  void ParentSpaceRoundTripRestoresTheAuthoredPose() {
    // Seating the proxy and writing it back must leave the camera unmoved.
    const auto parent = MakeParent();
    const auto local = MakeCameraPlacement();
    const auto world = ToWorldPlacement(local, parent);
    Assert::IsFalse(IsSamePlacement(local, world));

    const auto restored = ToParentPlacement(world, parent);
    AssertSame(local.position, restored.position);
    Assert::IsTrue(IsSamePlacement(local, restored));
  }

  [TestMethod]
  void ParentPlacementLandsOnTheWorldPose() {
    const auto parent = MakeParent();
    const CameraPlacement world {
      .position = { -6.0F, 12.0F, 1.75F },
      .rotation = glm::normalize(glm::quat { 0.2F, 0.7F, -0.1F, 0.6F }),
    };
    const auto local = ToParentPlacement(world, parent);
    Assert::AreEqual(1.0F, glm::length(local.rotation), kTolerance);

    const auto placed = ToWorldPlacement(local, parent);
    AssertSame(world.position, placed.position);
    Assert::IsTrue(IsSamePlacement(world, placed));
  }

  [TestMethod]
  void OppositeQuaternionsAreTheSamePlacement() {
    const auto pose = MakeCameraPlacement();
    const CameraPlacement negated { .position = pose.position, .rotation = -pose.rotation };
    Assert::IsTrue(IsSamePlacement(pose, negated));
  }

  [TestMethod]
  void AuthoringEditsAreNotTheSamePlacement() {
    const auto pose = MakeCameraPlacement();
    auto moved = pose;
    moved.position.y += 1.0e-3F;
    Assert::IsFalse(IsSamePlacement(pose, moved));

    auto turned = pose;
    turned.rotation = glm::normalize(
      glm::angleAxis(glm::radians(0.5F), glm::vec3 { 0.0F, 0.0F, 1.0F }) * pose.rotation);
    Assert::IsFalse(IsSamePlacement(pose, turned));
  }
};

} // namespace InteropTests
