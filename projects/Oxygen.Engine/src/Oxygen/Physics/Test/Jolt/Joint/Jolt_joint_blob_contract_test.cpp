//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltJointBlobContractTest : public JoltTestFixture { };

  [[nodiscard]] auto MakeInvalidNonCookedJointBlob() -> std::vector<uint8_t>
  {
    // Intentionally non-cooked payload envelope; strict restore must reject it.
    return std::vector<uint8_t> {
      'O',
      'P',
      'H',
      'B', // magic
      1U, // version
      1U, // kind = joint
      0U, // flavor
      0U, // reserved
      2U,
      0U,
      0U,
      0U, // payload_size = 2 ("{}")
      '{',
      '}',
    };
  }

  [[nodiscard]] auto MakeCookedFixedJointBlob() -> std::vector<uint8_t>
  {
    auto settings = JPH::FixedConstraintSettings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = true;

    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return {};
    }
    const auto blob = stream.str();
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(blob.data()),
      reinterpret_cast<const uint8_t*>(blob.data()) + blob.size());
  }

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

NOLINT_TEST_F(JoltJointBlobContractTest, CreateJointRejectsNonCookedBlobPayload)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

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

  joint::JointDesc desc {};
  desc.body_a = body_a;
  desc.body_b = body_b;
  const auto invalid_blob = MakeInvalidNonCookedJointBlob();
  desc.constraint_settings_blob = invalid_blob;
  const auto create_joint = joints.CreateJoint(world_id, desc);
  ASSERT_TRUE(create_joint.has_error());
  EXPECT_EQ(create_joint.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_b).has_value());
  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_a).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltJointBlobContractTest, CreateJointAcceptsBackendCookedBlobPayload)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

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

  const auto cooked_blob = MakeCookedFixedJointBlob();
  ASSERT_FALSE(cooked_blob.empty());

  joint::JointDesc desc {};
  desc.body_a = body_a;
  desc.body_b = body_b;
  desc.constraint_settings_blob = cooked_blob;
  const auto create_joint = joints.CreateJoint(world_id, desc);
  ASSERT_TRUE(create_joint.has_value());

  EXPECT_TRUE(joints.DestroyJoint(world_id, create_joint.value()).has_value());
  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_b).has_value());
  EXPECT_TRUE(system.Bodies().DestroyBody(world_id, body_a).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
