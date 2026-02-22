//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltJointDomainTest : public JoltTestFixture {
  protected:
    auto RequireBackend() -> void
    {
      AssertBackendAvailabilityContract();
      if (!HasBackend()) {
        GTEST_SKIP() << "No physics backend available.";
      }
    }
  };

  auto CreateDynamicBody(system::IPhysicsSystem& system, const WorldId world_id)
    -> BodyId
  {
    body::BodyDesc desc {};
    desc.type = body::BodyType::kDynamic;
    const auto body_result = system.Bodies().CreateBody(world_id, desc);
    EXPECT_TRUE(body_result.has_value());
    return body_result.has_value() ? body_result.value() : kInvalidBodyId;
  }

} // namespace

NOLINT_TEST_F(JoltJointDomainTest, EveryJointTypeSupportsCreateEnableDestroy)
{
  RequireBackend();

  auto& system = System();
  auto& worlds = system.Worlds();
  auto& joints = system.Joints();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto body_a = CreateDynamicBody(system, world_id);
  const auto body_b = CreateDynamicBody(system, world_id);
  ASSERT_NE(body_a, kInvalidBodyId);
  ASSERT_NE(body_b, kInvalidBodyId);

  constexpr std::array joint_types {
    joint::JointType::kFixed,
    joint::JointType::kDistance,
    joint::JointType::kHinge,
    joint::JointType::kSlider,
    joint::JointType::kSpherical,
  };

  for (const auto type : joint_types) {
    joint::JointDesc desc {};
    desc.type = type;
    desc.body_a = body_a;
    desc.body_b = body_b;
    desc.anchor_a = Vec3 { 0.0F, 0.0F, 0.0F };
    desc.anchor_b = Vec3 { 0.0F, 0.0F, 0.0F };

    const auto create_joint = joints.CreateJoint(world_id, desc);
    ASSERT_TRUE(create_joint.has_value()) << to_string(type);
    const auto joint_id = create_joint.value();
    ASSERT_NE(joint_id, kInvalidJointId);
    EXPECT_TRUE(joints.SetJointEnabled(world_id, joint_id, false).has_value());
    EXPECT_TRUE(joints.SetJointEnabled(world_id, joint_id, true).has_value());
    EXPECT_TRUE(joints.DestroyJoint(world_id, joint_id).has_value());
  }

  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_b).has_value());
  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_a).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
