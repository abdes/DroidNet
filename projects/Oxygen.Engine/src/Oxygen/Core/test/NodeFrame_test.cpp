//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Mat4;
using oxygen::Quat;
using oxygen::Vec3;
using oxygen::Vec4;
namespace node = oxygen::space::node;
namespace look = oxygen::space::look;
namespace move = oxygen::space::move;

constexpr float kTolerance = 1.0e-5F;

auto ExpectNear(const Vec3& actual, const Vec3& expected) -> void
{
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

//! The view-space direction of a world direction.
auto ToView(const Mat4& view, const Vec3& direction) -> Vec3
{
  const Vec4 result = view * Vec4(direction, 0.0F);
  return { result.x, result.y, result.z };
}

//! The node frame is right-handed: forward x up is right.
NOLINT_TEST(NodeFrameTest, IsRightHanded)
{
  ExpectNear(glm::cross(node::Forward, node::Up), node::Right);
  ExpectNear(node::Forward, move::Forward);
  ExpectNear(node::Up, move::Up);
}

//! An unrotated camera looks along world -Y with +Z up, the direction an
//! unrotated light shines along (`node::Forward`).
NOLINT_TEST(NodeFrameTest, UnrotatedCameraLooksWhereUnrotatedLightShines)
{
  const Vec3 position { 2.0F, -3.0F, 4.0F };
  const auto view
    = oxygen::transforms::NodeViewMatrix(position, Quat { 1, 0, 0, 0 });

  ExpectNear(ToView(view, Vec3 { 0.0F, -1.0F, 0.0F }), look::Forward);
  ExpectNear(ToView(view, Vec3 { 0.0F, 0.0F, 1.0F }), look::Up);
  ExpectNear(ToView(view, Vec3 { -1.0F, 0.0F, 0.0F }), look::Right);
  ExpectNear(Vec3(view * Vec4(position, 1.0F)), Vec3 { 0.0F });

  const Vec3 light_direction = Quat { 1, 0, 0, 0 } * node::Forward;
  ExpectNear(ToView(view, light_direction), look::Forward);
}

//! A rotated camera keeps its node's frame: it looks along its node's
//! forward with its node's up at the top and its node's right on the right.
NOLINT_TEST(NodeFrameTest, CameraViewFollowsNodeRotation)
{
  const auto rotation = glm::normalize(
    glm::angleAxis(0.7F, move::Up) * glm::angleAxis(-0.4F, Vec3 { 1, 0, 0 }));
  const auto view
    = oxygen::transforms::NodeViewMatrix(Vec3 { 1.0F, 5.0F, 2.0F }, rotation);

  ExpectNear(ToView(view, rotation * node::Forward), look::Forward);
  ExpectNear(ToView(view, rotation * node::Up), look::Up);
  ExpectNear(ToView(view, rotation * node::Right), look::Right);
}

//! LookRotation faces the node's forward along the requested direction, with
//! its up toward the requested up, as a proper rotation.
NOLINT_TEST(NodeFrameTest, LookRotationFacesDirectionWithUp)
{
  constexpr auto kDirections = std::to_array<Vec3>({
    { 0.0F, -1.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F },
    { 1.0F, 0.0F, 0.0F },
    { 3.0F, -4.0F, 2.0F },
    { -0.2F, 0.5F, -1.0F },
  });
  for (const auto& direction : kDirections) {
    SCOPED_TRACE(testing::Message()
      << direction.x << ", " << direction.y << ", " << direction.z);
    const auto rotation
      = oxygen::transforms::LookRotation({ .forward = direction });
    const auto forward = glm::normalize(direction);
    ExpectNear(rotation * node::Forward, forward);
    const auto up = rotation * node::Up;
    EXPECT_NEAR(glm::dot(up, forward), 0.0F, kTolerance);
    EXPECT_GT(glm::dot(up, move::Up), 0.0F);
    EXPECT_NEAR(glm::determinant(glm::mat3_cast(rotation)), 1.0F, kTolerance);
  }
}

//! The identity is the rotation that faces the node frame's own forward.
NOLINT_TEST(NodeFrameTest, LookRotationOfNodeForwardIsIdentity)
{
  const auto rotation
    = oxygen::transforms::LookRotation({ .forward = node::Forward });
  EXPECT_NEAR(std::abs(rotation.w), 1.0F, kTolerance);
}

//! Looking straight down or up keeps a valid frame, with up from the
//! explicit hint when one is given.
NOLINT_TEST(NodeFrameTest, LookRotationHandlesVerticalDirections)
{
  const auto down = oxygen::transforms::LookRotation(
    { .forward = move::Down, .up = move::Back });
  ExpectNear(down * node::Forward, move::Down);
  ExpectNear(down * node::Up, move::Back);

  for (const auto& direction : { move::Down, move::Up }) {
    const auto rotation
      = oxygen::transforms::LookRotation({ .forward = direction });
    ExpectNear(rotation * node::Forward, direction);
    EXPECT_NEAR(glm::length(rotation * node::Up), 1.0F, kTolerance);
    EXPECT_NEAR(glm::dot(rotation * node::Up, rotation * node::Forward), 0.0F,
      kTolerance);
  }
}

//! The world's up, forward and right land on hardware cube +Y, +Z and +X, by
//! a proper rotation that WorldFromCube undoes.
NOLINT_TEST(EnvironmentTest, CubeFromWorldIsTheHardwareCubeRotation)
{
  namespace env = oxygen::space::environment;
  ExpectNear(env::CubeFromWorld(move::Up), Vec3 { 0.0F, 1.0F, 0.0F });
  ExpectNear(env::CubeFromWorld(move::Forward), Vec3 { 0.0F, 0.0F, 1.0F });
  ExpectNear(env::CubeFromWorld(move::Right), Vec3 { 1.0F, 0.0F, 0.0F });
  const Vec3 direction { 0.3F, -0.5F, 0.8F };
  ExpectNear(env::WorldFromCube(env::CubeFromWorld(direction)), direction);
  const auto cube = glm::mat3(env::CubeFromWorld(Vec3 { 1, 0, 0 }),
    env::CubeFromWorld(Vec3 { 0, 1, 0 }), env::CubeFromWorld(Vec3 { 0, 0, 1 }));
  EXPECT_NEAR(glm::determinant(cube), 1.0F, kTolerance);
}

} // namespace
