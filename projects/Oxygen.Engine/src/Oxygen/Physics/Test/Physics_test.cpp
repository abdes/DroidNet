//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Area/AreaDesc.h>
#include <Oxygen/Physics/Articulation/ArticulationDesc.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/Physics.h>
#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Query/Sweep.h>
#include <Oxygen/Physics/Shape/ShapeDesc.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test {
namespace {

  class PhysicsApiContractTest : public testing::Test {
  protected:
    auto SetUp() -> void override { system_result_ = CreatePhysicsSystem(); }

    [[nodiscard]] auto HasBackend() const noexcept -> bool
    {
#if defined(OXGN_PHYS_BACKEND_JOLT)
      return true;
#elif defined(OXGN_PHYS_BACKEND_NONE)
      return false;
#else
#  error "No physics backend compile definition is set"
#endif
    }

    auto AssertBackendAvailabilityContract() const -> void
    {
#if defined(OXGN_PHYS_BACKEND_JOLT)
      ASSERT_TRUE(system_result_.has_value());
      ASSERT_TRUE(system_result_.value() != nullptr);
#elif defined(OXGN_PHYS_BACKEND_NONE)
      ASSERT_TRUE(system_result_.has_error());
      EXPECT_EQ(system_result_.error(), PhysicsError::kBackendUnavailable);
#else
#  error "No physics backend compile definition is set"
#endif
    }

    [[nodiscard]] auto System() -> system::IPhysicsSystem&
    {
      return *system_result_.value();
    }

  private:
    PhysicsResult<std::unique_ptr<system::IPhysicsSystem>> system_result_
      = Err(PhysicsError::kBackendUnavailable);
  };

} // namespace

NOLINT_TEST(Physics, BackendNameMatchesSelection)
{
#if defined(OXGN_PHYS_BACKEND_JOLT)
  EXPECT_EQ(GetBackendName(), "jolt");
  EXPECT_EQ(GetSelectedBackend(), PhysicsBackend::kJolt);
#elif defined(OXGN_PHYS_BACKEND_NONE)
  EXPECT_EQ(GetBackendName(), "none");
  EXPECT_EQ(GetSelectedBackend(), PhysicsBackend::kNone);
#else
#  error "No physics backend compile definition is set"
#endif
}

NOLINT_TEST_F(PhysicsApiContractTest, CreatePhysicsSystemFollowsBackendContract)
{
  AssertBackendAvailabilityContract();
}

NOLINT_TEST_F(PhysicsApiContractTest, DomainFacadeReferencesAreStable)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& system = System();
  auto& worlds = system.Worlds();
  auto& bodies = system.Bodies();
  auto& queries = system.Queries();
  auto& events = system.Events();
  auto& characters = system.Characters();
  auto& shapes = system.Shapes();
  auto& areas = system.Areas();
  auto& joints = system.Joints();
  EXPECT_EQ(&worlds, &system.Worlds());
  EXPECT_EQ(&bodies, &system.Bodies());
  EXPECT_EQ(&queries, &system.Queries());
  EXPECT_EQ(&events, &system.Events());
  EXPECT_EQ(&characters, &system.Characters());
  EXPECT_EQ(&shapes, &system.Shapes());
  EXPECT_EQ(&areas, &system.Areas());
  EXPECT_EQ(&joints, &system.Joints());

  const auto& const_system = system;
  EXPECT_EQ(static_cast<const void*>(&worlds),
    static_cast<const void*>(&const_system.Worlds()));
  EXPECT_EQ(static_cast<const void*>(&bodies),
    static_cast<const void*>(&const_system.Bodies()));
  EXPECT_EQ(static_cast<const void*>(&queries),
    static_cast<const void*>(&const_system.Queries()));
  EXPECT_EQ(static_cast<const void*>(&events),
    static_cast<const void*>(&const_system.Events()));
  EXPECT_EQ(static_cast<const void*>(&characters),
    static_cast<const void*>(&const_system.Characters()));
  EXPECT_EQ(static_cast<const void*>(&shapes),
    static_cast<const void*>(&const_system.Shapes()));
  EXPECT_EQ(static_cast<const void*>(&areas),
    static_cast<const void*>(&const_system.Areas()));
  EXPECT_EQ(static_cast<const void*>(&joints),
    static_cast<const void*>(&const_system.Joints()));

  const auto* aggregates = system.Aggregates();
  const auto* articulations = system.Articulations();
  const auto* vehicles = system.Vehicles();
  const auto* soft_bodies = system.SoftBodies();
  EXPECT_EQ(aggregates, system.Aggregates());
  EXPECT_EQ(articulations, system.Articulations());
  EXPECT_EQ(vehicles, system.Vehicles());
  EXPECT_EQ(soft_bodies, system.SoftBodies());

  const auto* const_aggregates = const_system.Aggregates();
  const auto* const_articulations = const_system.Articulations();
  const auto* const_vehicles = const_system.Vehicles();
  const auto* const_soft_bodies = const_system.SoftBodies();
  EXPECT_EQ(const_aggregates, const_system.Aggregates());
  EXPECT_EQ(const_articulations, const_system.Articulations());
  EXPECT_EQ(const_vehicles, const_system.Vehicles());
  EXPECT_EQ(const_soft_bodies, const_system.SoftBodies());
  if (aggregates == nullptr) {
    EXPECT_EQ(const_aggregates, nullptr);
  }
  if (articulations == nullptr) {
    EXPECT_EQ(const_articulations, nullptr);
  }
  if (vehicles == nullptr) {
    EXPECT_EQ(const_vehicles, nullptr);
  }
  if (soft_bodies == nullptr) {
    EXPECT_EQ(const_soft_bodies, nullptr);
  }
}

NOLINT_TEST_F(PhysicsApiContractTest, WorldLifecycleContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();
  EXPECT_NE(world_id, kInvalidWorldId);

  const auto step_ok = worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F);
  EXPECT_TRUE(step_ok.has_value());

  const auto destroy_ok = worlds.DestroyWorld(world_id);
  EXPECT_TRUE(destroy_ok.has_value());

  const auto step_after_destroy
    = worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F);
  EXPECT_TRUE(step_after_destroy.has_error());
}

NOLINT_TEST_F(PhysicsApiContractTest, WorldGravityRoundTripContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  const auto set_gravity
    = worlds.SetGravity(world_id, Vec3 { 0.0F, -4.0F, 0.0F });
  EXPECT_TRUE(set_gravity.has_value());

  const auto gravity = worlds.GetGravity(world_id);
  ASSERT_TRUE(gravity.has_value());
  EXPECT_FLOAT_EQ(gravity.value().x, 0.0F);
  EXPECT_FLOAT_EQ(gravity.value().y, -4.0F);
  EXPECT_FLOAT_EQ(gravity.value().z, 0.0F);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, InvalidWorldCallsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  EXPECT_TRUE(
    worlds.Step(kInvalidWorldId, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_error());
  EXPECT_TRUE(worlds.GetGravity(kInvalidWorldId).has_error());
  EXPECT_TRUE(worlds.DestroyWorld(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(PhysicsApiContractTest, InvalidBodyCallsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  EXPECT_TRUE(bodies.GetBodyPosition(world_id, kInvalidBodyId).has_error());
  EXPECT_TRUE(bodies.GetBodyRotation(world_id, kInvalidBodyId).has_error());
  EXPECT_TRUE(
    bodies.SetBodyPosition(world_id, kInvalidBodyId, Vec3 { 1.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(bodies
      .SetBodyRotation(
        world_id, kInvalidBodyId, Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(bodies
      .SetBodyPose(world_id, kInvalidBodyId, Vec3 { 0.0F, 1.0F, 0.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(bodies.GetLinearVelocity(world_id, kInvalidBodyId).has_error());
  EXPECT_TRUE(bodies.GetAngularVelocity(world_id, kInvalidBodyId).has_error());
  EXPECT_TRUE(bodies
      .SetLinearVelocity(world_id, kInvalidBodyId, Vec3 { 1.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(bodies
      .SetAngularVelocity(world_id, kInvalidBodyId, Vec3 { 0.0F, 1.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(
    bodies.AddForce(world_id, kInvalidBodyId, Vec3 { 0.0F, 0.0F, 1.0F })
      .has_error());
  EXPECT_TRUE(
    bodies.AddImpulse(world_id, kInvalidBodyId, Vec3 { 0.0F, 0.0F, 1.0F })
      .has_error());
  EXPECT_TRUE(
    bodies.AddTorque(world_id, kInvalidBodyId, Vec3 { 0.0F, 0.0F, 1.0F })
      .has_error());
  EXPECT_TRUE(bodies
      .MoveKinematic(world_id, kInvalidBodyId, Vec3 { 1.0F, 2.0F, 3.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F }, 1.0F / 60.0F)
      .has_error());
  EXPECT_TRUE(bodies.DestroyBody(world_id, kInvalidBodyId).has_error());
  EXPECT_TRUE(bodies
      .AddBodyShape(world_id, kInvalidBodyId, kInvalidShapeId,
        Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(
    bodies.RemoveBodyShape(world_id, kInvalidBodyId, kInvalidShapeInstanceId)
      .has_error());
  EXPECT_TRUE(bodies.FlushStructuralChanges(world_id).has_value());

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, BodyLifecycleAndPoseContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  desc.initial_position = Vec3 { 1.0F, 2.0F, 3.0F };
  const auto body_result = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto position = bodies.GetBodyPosition(world_id, body_id);
  ASSERT_TRUE(position.has_value());
  EXPECT_FLOAT_EQ(position.value().x, 1.0F);
  EXPECT_FLOAT_EQ(position.value().y, 2.0F);
  EXPECT_FLOAT_EQ(position.value().z, 3.0F);

  EXPECT_TRUE(bodies
      .SetBodyPose(world_id, body_id, Vec3 { 5.0F, 6.0F, 7.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_value());
  const auto moved = bodies.GetBodyPosition(world_id, body_id);
  ASSERT_TRUE(moved.has_value());
  EXPECT_FLOAT_EQ(moved.value().x, 5.0F);
  EXPECT_FLOAT_EQ(moved.value().y, 6.0F);
  EXPECT_FLOAT_EQ(moved.value().z, 7.0F);

  EXPECT_TRUE(
    bodies.SetLinearVelocity(world_id, body_id, Vec3 { 0.5F, 0.0F, 0.0F })
      .has_value());
  const auto linear_velocity = bodies.GetLinearVelocity(world_id, body_id);
  EXPECT_TRUE(linear_velocity.has_value());

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(bodies.GetBodyPosition(world_id, body_id).has_error());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, BodyBulkPoseAndKinematicBatchContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc dynamic_desc {};
  dynamic_desc.type = body::BodyType::kDynamic;
  dynamic_desc.initial_position = Vec3 { 1.0F, 2.0F, 3.0F };
  const auto dynamic_body = bodies.CreateBody(world_id, dynamic_desc);
  ASSERT_TRUE(dynamic_body.has_value());

  body::BodyDesc kinematic_desc {};
  kinematic_desc.type = body::BodyType::kKinematic;
  const auto kinematic_body = bodies.CreateBody(world_id, kinematic_desc);
  ASSERT_TRUE(kinematic_body.has_value());

  std::vector<BodyId> ids {
    dynamic_body.value(),
    kinematic_body.value(),
  };
  std::vector<Vec3> positions(ids.size(), Vec3 { 0.0F });
  std::vector<Quat> rotations(ids.size(), Quat { 1.0F, 0.0F, 0.0F, 0.0F });
  const auto bulk_get
    = bodies.GetBodyPoses(world_id, ids, positions, rotations);
  ASSERT_TRUE(bulk_get.has_value());
  EXPECT_EQ(bulk_get.value(), ids.size());
  EXPECT_FLOAT_EQ(positions[0].x, 1.0F);
  EXPECT_FLOAT_EQ(positions[0].y, 2.0F);
  EXPECT_FLOAT_EQ(positions[0].z, 3.0F);

  std::vector<BodyId> kinematic_ids { kinematic_body.value() };
  std::vector<Vec3> target_positions { Vec3 { 4.0F, 5.0F, 6.0F } };
  std::vector<Quat> target_rotations { Quat { 1.0F, 0.0F, 0.0F, 0.0F } };
  const auto bulk_move = bodies.MoveKinematicBatch(
    world_id, kinematic_ids, target_positions, target_rotations, 1.0F / 60.0F);
  ASSERT_TRUE(bulk_move.has_value());
  EXPECT_EQ(bulk_move.value(), kinematic_ids.size());

  EXPECT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto kinematic_pos
    = bodies.GetBodyPosition(world_id, kinematic_body.value());
  ASSERT_TRUE(kinematic_pos.has_value());
  EXPECT_NEAR(kinematic_pos.value().x, 4.0F, 0.05F);
  EXPECT_NEAR(kinematic_pos.value().y, 5.0F, 0.05F);
  EXPECT_NEAR(kinematic_pos.value().z, 6.0F, 0.05F);

  EXPECT_TRUE(bodies.DestroyBody(world_id, dynamic_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, kinematic_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  PhysicsApiContractTest, BodyStructuralFlushInvalidWorldReturnsError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& bodies = System().Bodies();
  const auto flush = bodies.FlushStructuralChanges(kInvalidWorldId);
  EXPECT_TRUE(flush.has_error());
}

NOLINT_TEST_F(PhysicsApiContractTest, AggregateLifecycleContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  if (aggregates == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto create_world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_world.has_value());
  const auto world_id = create_world.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto create_aggregate = aggregates->CreateAggregate(world_id);
  ASSERT_TRUE(create_aggregate.has_value());
  const auto aggregate_id = create_aggregate.value();
  EXPECT_TRUE(IsValid(aggregate_id));

  EXPECT_TRUE(
    aggregates->AddMemberBody(world_id, aggregate_id, body_id).has_value());

  std::vector<BodyId> members(4, kInvalidBodyId);
  const auto get_members
    = aggregates->GetMemberBodies(world_id, aggregate_id, members);
  ASSERT_TRUE(get_members.has_value());
  EXPECT_GE(get_members.value(), 1U);

  EXPECT_TRUE(
    aggregates->RemoveMemberBody(world_id, aggregate_id, body_id).has_value());
  EXPECT_TRUE(aggregates->DestroyAggregate(world_id, aggregate_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  PhysicsApiContractTest, AggregateStructuralFlushInvalidWorldIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  if (aggregates == nullptr) {
    return;
  }

  EXPECT_TRUE(aggregates->FlushStructuralChanges(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(PhysicsApiContractTest, AggregateRebindContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  if (aggregates == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto create_world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_world.has_value());
  const auto world_id = create_world.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto aggregate_a = aggregates->CreateAggregate(world_id);
  ASSERT_TRUE(aggregate_a.has_value());
  const auto aggregate_a_id = aggregate_a.value();

  const auto aggregate_b = aggregates->CreateAggregate(world_id);
  ASSERT_TRUE(aggregate_b.has_value());
  const auto aggregate_b_id = aggregate_b.value();

  ASSERT_TRUE(
    aggregates->AddMemberBody(world_id, aggregate_a_id, body_id).has_value());
  ASSERT_TRUE(aggregates->RemoveMemberBody(world_id, aggregate_a_id, body_id)
      .has_value());
  ASSERT_TRUE(
    aggregates->AddMemberBody(world_id, aggregate_b_id, body_id).has_value());

  std::vector<BodyId> members_b(4, kInvalidBodyId);
  const auto get_members_b
    = aggregates->GetMemberBodies(world_id, aggregate_b_id, members_b);
  ASSERT_TRUE(get_members_b.has_value());
  EXPECT_GE(get_members_b.value(), 1U);

  EXPECT_TRUE(aggregates->RemoveMemberBody(world_id, aggregate_b_id, body_id)
      .has_value());
  EXPECT_TRUE(
    aggregates->DestroyAggregate(world_id, aggregate_a_id).has_value());
  EXPECT_TRUE(
    aggregates->DestroyAggregate(world_id, aggregate_b_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, CharacterLifecycleContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& characters = System().Characters();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  const auto create_character
    = characters.CreateCharacter(world_id, character::CharacterDesc {});
  ASSERT_TRUE(create_character.has_value());
  const auto character_id = create_character.value();
  EXPECT_NE(character_id, kInvalidCharacterId);

  const auto move = characters.MoveCharacter(
    world_id, character_id, character::CharacterMoveInput {}, 1.0F / 60.0F);
  EXPECT_TRUE(move.has_value());
  if (move.has_value()) {
    EXPECT_TRUE(std::isfinite(move.value().state.position.x));
    EXPECT_TRUE(std::isfinite(move.value().state.position.y));
    EXPECT_TRUE(std::isfinite(move.value().state.position.z));
    EXPECT_TRUE(std::isfinite(move.value().state.rotation.w));
  }

  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_value());
  EXPECT_TRUE(characters
      .MoveCharacter(
        world_id, character_id, character::CharacterMoveInput {}, 1.0F / 60.0F)
      .has_error());
  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_error());

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, InvalidCharacterCallsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& characters = System().Characters();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  EXPECT_TRUE(
    characters.DestroyCharacter(world_id, kInvalidCharacterId).has_error());
  EXPECT_TRUE(characters
      .MoveCharacter(world_id, kInvalidCharacterId,
        character::CharacterMoveInput {}, 1.0F / 60.0F)
      .has_error());

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  PhysicsApiContractTest, ArticulationAuthorityAndFlushContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  if (articulations == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto root_body = body_result.value();

  const auto articulation_result = articulations->CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = root_body,
    });
  ASSERT_TRUE(articulation_result.has_value());
  const auto articulation_id = articulation_result.value();

  const auto authority = articulations->GetAuthority(world_id, articulation_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_TRUE(authority.value() == aggregate::AggregateAuthority::kSimulation
    || authority.value() == aggregate::AggregateAuthority::kCommand);

  EXPECT_TRUE(articulations->FlushStructuralChanges(world_id).has_value());
  EXPECT_TRUE(
    articulations->FlushStructuralChanges(kInvalidWorldId).has_error());

  EXPECT_TRUE(
    articulations->DestroyArticulation(world_id, articulation_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, root_body).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, ArticulationLifecycleContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  if (articulations == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc root_desc {};
  root_desc.type = body::BodyType::kDynamic;
  const auto root_result = bodies.CreateBody(world_id, root_desc);
  ASSERT_TRUE(root_result.has_value());
  const auto root_id = root_result.value();

  const auto articulation_result = articulations->CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = root_id,
    });
  ASSERT_TRUE(articulation_result.has_value());
  const auto articulation_id = articulation_result.value();
  EXPECT_TRUE(IsValid(articulation_id));

  const auto root_query = articulations->GetRootBody(world_id, articulation_id);
  ASSERT_TRUE(root_query.has_value());
  EXPECT_EQ(root_query.value(), root_id);

  std::vector<BodyId> links(4, kInvalidBodyId);
  const auto links_query
    = articulations->GetLinkBodies(world_id, articulation_id, links);
  ASSERT_TRUE(links_query.has_value());
  EXPECT_EQ(links_query.value(), 0U);

  EXPECT_TRUE(
    articulations->DestroyArticulation(world_id, articulation_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, root_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, ArticulationLinkContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  if (articulations == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto parent_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(parent_result.has_value());
  const auto parent_body = parent_result.value();

  const auto child_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(child_result.has_value());
  const auto child_body = child_result.value();

  const auto articulation_result = articulations->CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = parent_body,
    });
  ASSERT_TRUE(articulation_result.has_value());
  const auto articulation_id = articulation_result.value();

  EXPECT_TRUE(articulations
      ->AddLink(world_id, articulation_id,
        articulation::ArticulationLinkDesc {
          .parent_body_id = parent_body,
          .child_body_id = child_body,
        })
      .has_value());

  std::vector<BodyId> links(8, kInvalidBodyId);
  const auto links_query
    = articulations->GetLinkBodies(world_id, articulation_id, links);
  ASSERT_TRUE(links_query.has_value());
  EXPECT_GE(links_query.value(), 1U);

  EXPECT_TRUE(articulations->RemoveLink(world_id, articulation_id, child_body)
      .has_value());
  EXPECT_TRUE(
    articulations->DestroyArticulation(world_id, articulation_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, child_body).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, parent_body).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  PhysicsApiContractTest, VehicleAuthorityAndFlushContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* vehicles = System().Vehicles();
  if (vehicles == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto chassis_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis_result.has_value());
  const auto chassis_body = chassis_result.value();

  const auto wheel_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(wheel_result.has_value());
  const auto wheel_body = wheel_result.value();
  const auto wheel2_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(wheel2_result.has_value());
  const auto wheel_body2 = wheel2_result.value();

  const std::array<BodyId, 2> wheel_ids {
    wheel_body,
    wheel_body2,
  };
  const auto vehicle_result = vehicles->CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis_body,
      .wheel_body_ids = wheel_ids,
    });
  ASSERT_TRUE(vehicle_result.has_value());
  const auto vehicle_id = vehicle_result.value();

  const auto authority = vehicles->GetAuthority(world_id, vehicle_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_TRUE(authority.value() == aggregate::AggregateAuthority::kSimulation
    || authority.value() == aggregate::AggregateAuthority::kCommand);

  EXPECT_TRUE(vehicles->FlushStructuralChanges(world_id).has_value());
  EXPECT_TRUE(vehicles->FlushStructuralChanges(kInvalidWorldId).has_error());

  EXPECT_TRUE(vehicles->DestroyVehicle(world_id, vehicle_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_body2).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_body).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis_body).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, VehicleLifecycleContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* vehicles = System().Vehicles();
  if (vehicles == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;

  const auto chassis_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis_result.has_value());
  const auto chassis_body = chassis_result.value();

  const auto wheel_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(wheel_result.has_value());
  const auto wheel_body = wheel_result.value();
  const auto wheel2_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(wheel2_result.has_value());
  const auto wheel_body2 = wheel2_result.value();

  const std::array<BodyId, 2> wheel_ids {
    wheel_body,
    wheel_body2,
  };
  const auto vehicle_result = vehicles->CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis_body,
      .wheel_body_ids = wheel_ids,
    });
  ASSERT_TRUE(vehicle_result.has_value());
  const auto vehicle_id = vehicle_result.value();
  EXPECT_TRUE(IsValid(vehicle_id));

  EXPECT_TRUE(vehicles
      ->SetControlInput(world_id, vehicle_id,
        vehicle::VehicleControlInput {
          .throttle = 0.5F,
          .brake = 0.0F,
          .steering = 0.25F,
          .handbrake = 0.0F,
        })
      .has_value());
  const auto state = vehicles->GetState(world_id, vehicle_id);
  ASSERT_TRUE(state.has_value());

  EXPECT_TRUE(vehicles->DestroyVehicle(world_id, vehicle_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_body2).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_body).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis_body).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  PhysicsApiContractTest, VehicleDestroyAfterWorldDestroyContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* vehicles = System().Vehicles();
  if (vehicles == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto chassis_result = bodies.CreateBody(world_id, body_desc);
  const auto wheel_a_result = bodies.CreateBody(world_id, body_desc);
  const auto wheel_b_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis_result.has_value());
  ASSERT_TRUE(wheel_a_result.has_value());
  ASSERT_TRUE(wheel_b_result.has_value());

  const std::array<BodyId, 2> wheel_ids {
    wheel_a_result.value(),
    wheel_b_result.value(),
  };
  const auto vehicle_result = vehicles->CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis_result.value(),
      .wheel_body_ids = wheel_ids,
    });
  ASSERT_TRUE(vehicle_result.has_value());
  const auto vehicle_id = vehicle_result.value();

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());

  const auto destroy_after_world
    = vehicles->DestroyVehicle(world_id, vehicle_id);
  ASSERT_TRUE(destroy_after_world.has_error());
  EXPECT_EQ(destroy_after_world.error(), PhysicsError::kWorldNotFound);

  const auto destroy_again = vehicles->DestroyVehicle(world_id, vehicle_id);
  ASSERT_TRUE(destroy_again.has_error());
  EXPECT_EQ(destroy_again.error(), PhysicsError::kInvalidArgument);
}

NOLINT_TEST_F(
  PhysicsApiContractTest, SoftBodyAuthorityAndFlushContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  if (soft_bodies == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto create_result = soft_bodies->CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 1U,
    });
  ASSERT_TRUE(create_result.has_value());
  const auto soft_body_id = create_result.value();

  const auto authority = soft_bodies->GetAuthority(world_id, soft_body_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_TRUE(authority.value() == aggregate::AggregateAuthority::kSimulation
    || authority.value() == aggregate::AggregateAuthority::kCommand);

  EXPECT_TRUE(soft_bodies->FlushStructuralChanges(world_id).has_value());
  EXPECT_TRUE(soft_bodies->FlushStructuralChanges(kInvalidWorldId).has_error());

  EXPECT_TRUE(soft_bodies->DestroySoftBody(world_id, soft_body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, SoftBodyLifecycleContractIfSupported)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  if (soft_bodies == nullptr) {
    return;
  }

  auto& worlds = System().Worlds();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto create_result = soft_bodies->CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 8U,
    });
  ASSERT_TRUE(create_result.has_value());
  const auto soft_body_id = create_result.value();
  EXPECT_TRUE(IsValid(soft_body_id));

  EXPECT_TRUE(soft_bodies
      ->SetMaterialParams(world_id, soft_body_id,
        softbody::SoftBodyMaterialParams {
          .stiffness = 0.7F,
          .damping = 0.2F,
        })
      .has_value());

  const auto state = soft_bodies->GetState(world_id, soft_body_id);
  ASSERT_TRUE(state.has_value());

  EXPECT_TRUE(soft_bodies->DestroySoftBody(world_id, soft_body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, ShapeLifecycleContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& shapes = System().Shapes();
  const auto create_shape = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(create_shape.has_value());
  const auto shape_id = create_shape.value();
  EXPECT_NE(shape_id, kInvalidShapeId);
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_error());
}

NOLINT_TEST_F(PhysicsApiContractTest, DestroyShapeWhileAttachedReturnsError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& shapes = System().Shapes();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto shape_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(shape_result.has_value());
  const auto shape_id = shape_result.value();

  body::BodyDesc desc {};
  const auto body_result = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto attach_result = bodies.AddBodyShape(world_id, body_id, shape_id,
    Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F });
  ASSERT_TRUE(attach_result.has_value());
  const auto instance_id = attach_result.value();

  const auto destroy_attached = shapes.DestroyShape(shape_id);
  EXPECT_TRUE(destroy_attached.has_error());
  if (destroy_attached.has_error()) {
    EXPECT_EQ(destroy_attached.error(), PhysicsError::kAlreadyExists);
  }

  EXPECT_TRUE(
    bodies.RemoveBodyShape(world_id, body_id, instance_id).has_value());
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, InvalidAreaCallsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& areas = System().Areas();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  EXPECT_TRUE(areas.GetAreaPosition(world_id, kInvalidAreaId).has_error());
  EXPECT_TRUE(areas.GetAreaRotation(world_id, kInvalidAreaId).has_error());
  EXPECT_TRUE(areas
      .SetAreaPose(world_id, kInvalidAreaId, Vec3 { 0.0F, 0.0F, 0.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(areas
      .AddAreaShape(world_id, kInvalidAreaId, kInvalidShapeId,
        Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(
    areas.RemoveAreaShape(world_id, kInvalidAreaId, kInvalidShapeInstanceId)
      .has_error());
  EXPECT_TRUE(areas.DestroyArea(world_id, kInvalidAreaId).has_error());

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, InvalidJointCallsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& joints = System().Joints();
  const auto create_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto world_id = create_result.value();

  const auto destroy_invalid = joints.DestroyJoint(world_id, kInvalidJointId);
  ASSERT_TRUE(destroy_invalid.has_error());
  EXPECT_EQ(destroy_invalid.error(), PhysicsError::kInvalidArgument);
  const auto enable_invalid
    = joints.SetJointEnabled(world_id, kInvalidJointId, true);
  ASSERT_TRUE(enable_invalid.has_error());
  EXPECT_EQ(enable_invalid.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, QueryContractsOnEmptyWorld)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& queries = System().Queries();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto raycast = queries.Raycast(world_id, query::RaycastDesc {});
  ASSERT_TRUE(raycast.has_value());
  EXPECT_FALSE(raycast.value().has_value());

  std::vector<query::SweepHit> sweep_hits(8);
  const auto sweep = queries.Sweep(world_id, query::SweepDesc {}, sweep_hits);
  ASSERT_TRUE(sweep.has_value());
  EXPECT_EQ(sweep.value(), 0U);

  std::vector<uint64_t> overlap_hits(8, 0U);
  const auto overlap
    = queries.Overlap(world_id, query::OverlapDesc {}, overlap_hits);
  ASSERT_TRUE(overlap.has_value());
  EXPECT_EQ(overlap.value(), 0U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(PhysicsApiContractTest, EventContractsOnEmptyWorld)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& events = System().Events();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending.value(), 0U);

  std::vector<events::PhysicsEvent> event_buffer(8);
  const auto drained = events.DrainEvents(world_id, event_buffer);
  ASSERT_TRUE(drained.has_value());
  EXPECT_EQ(drained.value(), 0U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST(Physics, ToStringConvertersFollowContract)
{
  EXPECT_EQ(to_string(PhysicsBackend::kNone), "none");
  EXPECT_EQ(to_string(PhysicsError::kBackendUnavailable), "BackendUnavailable");
  EXPECT_STREQ(body::to_string(body::BodyType::kKinematic), "Kinematic");
  EXPECT_EQ(to_string(body::BodyFlags::kNone), "None");
  EXPECT_EQ(
    to_string(body::BodyFlags::kEnableGravity | body::BodyFlags::kIsTrigger),
    "EnableGravity|IsTrigger");
  EXPECT_EQ(to_string(events::PhysicsEventType::kContactBegin), "ContactBegin");
  EXPECT_EQ(to_string(WorldId { 7U }), "WorldId{7}");
  EXPECT_EQ(to_string(BodyId { 9U }), "BodyId{9}");
  EXPECT_EQ(to_string(CharacterId { 11U }), "CharacterId{11}");
  EXPECT_EQ(to_string(ShapeId { 13U }), "ShapeId{13}");
  EXPECT_EQ(to_string(ShapeInstanceId { 15U }), "ShapeInstanceId{15}");
  EXPECT_EQ(to_string(AreaId { 17U }), "AreaId{17}");
  EXPECT_EQ(to_string(JointId { 19U }), "JointId{19}");
  EXPECT_EQ(to_string(AggregateId { 21U }), "AggregateId{21}");
  EXPECT_EQ(aggregate::to_string(aggregate::AggregateAuthority::kSimulation),
    "Simulation");
  EXPECT_EQ(
    aggregate::to_string(aggregate::AggregateAuthority::kCommand), "Command");
  EXPECT_EQ(to_string(joint::JointType::kHinge), "Hinge");
  EXPECT_EQ(
    softbody::to_string(softbody::SoftBodyTetherMode::kGeodesic), "Geodesic");
}

} // namespace oxygen::physics::test
