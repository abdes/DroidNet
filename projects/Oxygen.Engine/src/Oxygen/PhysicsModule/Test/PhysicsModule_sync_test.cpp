//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "PhysicsModule_test_fixture.h"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace oxygen::physics::test {
namespace {

  auto ExpectVec3Eq(const Vec3& actual, const Vec3& expected) -> void
  {
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  }

  auto ExpectVec3Near(
    const Vec3& actual, const Vec3& expected, const float eps = 1.0e-4F) -> void
  {
    EXPECT_NEAR(actual.x, expected.x, eps);
    EXPECT_NEAR(actual.y, expected.y, eps);
    EXPECT_NEAR(actual.z, expected.z, eps);
  }

} // namespace

NOLINT_TEST_F(PhysicsModuleSyncTest, FixedSimulationStepsWorldOnce)
{
  RunFixedSimulation();
  EXPECT_EQ(FakeState().step_count, 1U);
  EXPECT_GT(FakeState().last_step_dt, 0.0F);
  EXPECT_GT(FakeState().last_step_fixed_dt, 0.0F);
}

NOLINT_TEST_F(
  PhysicsModuleSyncTest, AttachRigidBodySucceedsForObservedSceneNode)
{
  auto node = scene_->CreateNode("observed");
  ASSERT_TRUE(node.IsValid());

  RunGameplay();
  scene_->Update();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  const auto body = ScenePhysics::AttachRigidBody(
    observer_ptr<PhysicsModule> { module_.get() }, node, desc);
  ASSERT_TRUE(body.has_value());
}

NOLINT_TEST_F(
  PhysicsModuleSyncTest, AttachCharacterSucceedsForObservedSceneNode)
{
  auto node = scene_->CreateNode("observed-character");
  ASSERT_TRUE(node.IsValid());

  RunGameplay();
  scene_->Update();

  const auto character = ScenePhysics::AttachCharacter(
    observer_ptr<PhysicsModule> { module_.get() }, node,
    character::CharacterDesc {});
  ASSERT_TRUE(character.has_value());
  EXPECT_NE(character->GetCharacterId(), kInvalidCharacterId);
  EXPECT_EQ(FakeState().character_create_calls, 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, GetCharacterReturnsFacadeForAttachedNode)
{
  auto node = scene_->CreateNode("character-get");
  ASSERT_TRUE(node.IsValid());
  RunGameplay();
  scene_->Update();

  const auto attached = ScenePhysics::AttachCharacter(
    observer_ptr<PhysicsModule> { module_.get() }, node,
    character::CharacterDesc {});
  ASSERT_TRUE(attached.has_value());

  const auto queried = ScenePhysics::GetCharacter(
    observer_ptr<PhysicsModule> { module_.get() }, node.GetHandle());
  ASSERT_TRUE(queried.has_value());
  EXPECT_EQ(queried->GetNode(), node.GetHandle());
  EXPECT_EQ(queried->GetCharacterId(), attached->GetCharacterId());

  const auto move
    = queried->Move(character::CharacterMoveInput { .desired_velocity
                      = Vec3 { 1.0F, 2.0F, 3.0F } },
      1.0F / 60.0F);
  ASSERT_TRUE(move.has_value());
  EXPECT_EQ(FakeState().character_move_calls, 1U);
}

// In release builds, contract violations return nullopt.
#if defined(NDEBUG)
NOLINT_TEST_F(PhysicsModuleSyncTest, AttachRigidBodyFailsForUnobservedSceneNode)
{
  auto observed_node = scene_->CreateNode("observed");
  ASSERT_TRUE(observed_node.IsValid());

  RunGameplay();
  scene_->Update();

  auto other_scene = std::make_shared<scene::Scene>("OtherScene", 32);
  auto other_node = other_scene->CreateNode("other");
  ASSERT_TRUE(other_node.IsValid());
  other_scene->Update();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  const auto body = ScenePhysics::AttachRigidBody(
    observer_ptr<PhysicsModule> { module_.get() }, other_node, desc);
  EXPECT_FALSE(body.has_value());
}
#endif

// In assertions-enabled builds, the same contract violation is fatal.
#if !defined(NDEBUG)
NOLINT_TEST_F(PhysicsModuleSyncTest, AttachRigidBodyUnobservedSceneNode_Death)
{
  auto observed_node = scene_->CreateNode("observed");
  ASSERT_TRUE(observed_node.IsValid());

  RunGameplay();
  scene_->Update();

  auto other_scene = std::make_shared<scene::Scene>("OtherSceneDeath", 32);
  auto other_node = other_scene->CreateNode("other");
  ASSERT_TRUE(other_node.IsValid());
  other_scene->Update();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;

  NOLINT_EXPECT_DEATH(
    (void)ScenePhysics::AttachRigidBody(
      observer_ptr<PhysicsModule> { module_.get() }, other_node, desc),
    ".*");
}
#endif

NOLINT_TEST_F(PhysicsModuleSyncTest, GameplayPushesOnlyKinematicBodies)
{
  auto node = scene_->CreateNode("kinematic");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(AttachBody(node, body::BodyType::kKinematic).has_value());

  RunGameplay();

  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 7.0F, 2.0F, -3.0F }));
  scene_->Update();
  scene_->SyncObservers();

  RunGameplay();

  EXPECT_EQ(FakeState().move_kinematic_calls, 1U);
  ExpectVec3Eq(FakeState().last_moved_position, Vec3(7.0F, 2.0F, -3.0F));
  const auto stats = module_->GetSyncDiagnostics();
  EXPECT_EQ(stats.gameplay_push_success, 1U);
  EXPECT_EQ(stats.gameplay_push_skipped_non_kinematic, 0U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, GameplayIgnoresStaticBodies)
{
  auto node = scene_->CreateNode("static");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(AttachBody(node, body::BodyType::kStatic).has_value());

  RunGameplay();

  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 2.0F, 4.0F, 6.0F }));
  scene_->Update();
  scene_->SyncObservers();

  RunGameplay();

  EXPECT_EQ(FakeState().move_kinematic_calls, 0U);
  EXPECT_EQ(FakeState().set_body_pose_calls, 0U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneMutationPullsDynamicBodiesToRootLocal)
{
  auto node = scene_->CreateNode("dynamic-root");
  ASSERT_TRUE(node.IsValid());
  const auto body = AttachBody(node, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  RunGameplay();

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 11.0F, -2.0F, 5.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();

  const auto local_position = node.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local_position.has_value());
  ExpectVec3Eq(*local_position, Vec3(11.0F, -2.0F, 5.0F));
  const auto stats = module_->GetSyncDiagnostics();
  EXPECT_EQ(stats.scene_pull_success, 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest,
  SceneMutationPullsDynamicBodiesUsingParentLocalConversion)
{
  auto parent = scene_->CreateNode("parent");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(parent.GetTransform().SetLocalPosition({ 10.0F, 0.0F, 0.0F }));

  auto child_opt = scene_->CreateChildNode(parent, "child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;
  ASSERT_TRUE(child.IsValid());

  scene_->Update();

  const auto body = AttachBody(child, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  RunGameplay();

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 13.0F, 1.0F, 0.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();

  const auto child_local = child.GetTransform().GetLocalPosition();
  ASSERT_TRUE(child_local.has_value());
  ExpectVec3Eq(*child_local, Vec3(3.0F, 1.0F, 0.0F));
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneSwitchDestroysPreviousSceneBodies)
{
  auto scene_a = scene_;
  auto node_a = scene_a->CreateNode("scene-a-node");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(AttachBody(node_a, body::BodyType::kDynamic).has_value());
  EXPECT_EQ(FakeState().bodies.size(), 1U);

  auto scene_b = std::make_shared<scene::Scene>("SceneB", 64);
  auto node_b = scene_b->CreateNode("scene-b-node");
  ASSERT_TRUE(node_b.IsValid());
  SetActiveScene(scene_b);
  RunGameplay();
  EXPECT_EQ(FakeState().bodies.size(), 0U);

  ASSERT_TRUE(AttachBody(node_b, body::BodyType::kKinematic).has_value());
  EXPECT_EQ(FakeState().bodies.size(), 1U);

  SetActiveScene(scene_a);
  RunGameplay();
  EXPECT_EQ(FakeState().bodies.size(), 0U);
  ASSERT_TRUE(AttachBody(node_a, body::BodyType::kDynamic).has_value());
  EXPECT_EQ(FakeState().bodies.size(), 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneSwitchDestroysPreviousSceneCharacters)
{
  auto scene_a = scene_;
  auto node_a = scene_a->CreateNode("scene-a-character");
  ASSERT_TRUE(node_a.IsValid());
  RunGameplay();
  scene_a->Update();
  ASSERT_TRUE(
    ScenePhysics::AttachCharacter(observer_ptr<PhysicsModule> { module_.get() },
      node_a, character::CharacterDesc {})
      .has_value());
  EXPECT_EQ(FakeState().characters.size(), 1U);

  auto scene_b = std::make_shared<scene::Scene>("SceneBCharacter", 64);
  auto node_b = scene_b->CreateNode("scene-b-character");
  ASSERT_TRUE(node_b.IsValid());
  SetActiveScene(scene_b);
  RunGameplay();
  EXPECT_EQ(FakeState().characters.size(), 0U);

  scene_b->Update();
  ASSERT_TRUE(
    ScenePhysics::AttachCharacter(observer_ptr<PhysicsModule> { module_.get() },
      node_b, character::CharacterDesc {})
      .has_value());
  EXPECT_EQ(FakeState().characters.size(), 1U);

  SetActiveScene(scene_a);
  RunGameplay();
  EXPECT_EQ(FakeState().characters.size(), 0U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, DestroyAndReattachSameFrameKeepsStateValid)
{
  auto node = scene_->CreateNode("replace-node");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(AttachBody(node, body::BodyType::kDynamic).has_value());
  EXPECT_EQ(FakeState().bodies.size(), 1U);

  ASSERT_TRUE(scene_->DestroyNode(node));
  scene_->SyncObservers();
  EXPECT_EQ(FakeState().bodies.size(), 0U);

  auto replacement = scene_->CreateNode("replace-node");
  ASSERT_TRUE(replacement.IsValid());
  ASSERT_TRUE(AttachBody(replacement, body::BodyType::kDynamic).has_value());
  EXPECT_EQ(FakeState().bodies.size(), 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, DestroyNodeDestroysTrackedCharacter)
{
  auto node = scene_->CreateNode("character-node");
  ASSERT_TRUE(node.IsValid());
  RunGameplay();
  scene_->Update();
  const auto character = ScenePhysics::AttachCharacter(
    observer_ptr<PhysicsModule> { module_.get() }, node,
    character::CharacterDesc {});
  ASSERT_TRUE(character.has_value());
  ASSERT_EQ(FakeState().characters.size(), 1U);

  ASSERT_TRUE(scene_->DestroyNode(node));
  scene_->SyncObservers();

  EXPECT_TRUE(FakeState().characters.empty());
  EXPECT_EQ(FakeState().character_destroy_calls, 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, BatchAttachDestroyLeavesNoBodies)
{
  std::vector<scene::SceneNode> nodes;
  constexpr size_t kCount = 24;
  nodes.reserve(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    auto node = scene_->CreateNode("batch");
    ASSERT_TRUE(node.IsValid());
    ASSERT_TRUE(AttachBody(node, body::BodyType::kDynamic).has_value());
    nodes.push_back(node);
  }
  EXPECT_EQ(FakeState().bodies.size(), kCount);

  for (auto& node : nodes) {
    ASSERT_TRUE(scene_->DestroyNode(node));
  }
  scene_->SyncObservers();
  EXPECT_EQ(FakeState().bodies.size(), 0U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest,
  DestroyNodeBeforeFirstGameplayBindDoesNotCreateBodyState)
{
  auto node = scene_->CreateNode("ephemeral");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(scene_->DestroyNode(node));
  scene_->SyncObservers();

  RunGameplay();
  EXPECT_TRUE(FakeState().bodies.empty());
}

NOLINT_TEST_F(PhysicsModuleSyncTest, DynamicConflictSameFrameDynamicWins)
{
  auto node = scene_->CreateNode("dynamic-conflict");
  ASSERT_TRUE(node.IsValid());
  const auto body = AttachBody(node, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 1.0F, 1.0F, 1.0F }));
  scene_->Update();
  scene_->SyncObservers();

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 9.0F, 8.0F, 7.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunGameplay();
  RunSceneMutation();

  const auto local = node.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  ExpectVec3Eq(*local, Vec3(9.0F, 8.0F, 7.0F));
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneMutationDoesNotPullKinematicBody)
{
  auto node = scene_->CreateNode("kinematic-no-pull");
  ASSERT_TRUE(node.IsValid());
  const auto body = AttachBody(node, body::BodyType::kKinematic);
  ASSERT_TRUE(body.has_value());

  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 4.0F, 5.0F, 6.0F }));
  scene_->Update();

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 100.0F, 200.0F, 300.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();

  const auto local = node.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  ExpectVec3Eq(*local, Vec3(4.0F, 5.0F, 6.0F));
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneMutationDoesNotPullStaticBody)
{
  auto node = scene_->CreateNode("static-no-pull");
  ASSERT_TRUE(node.IsValid());
  const auto body = AttachBody(node, body::BodyType::kStatic);
  ASSERT_TRUE(body.has_value());

  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 2.0F, 3.0F, 4.0F }));
  scene_->Update();

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { -10.0F, -20.0F, -30.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();

  const auto local = node.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  ExpectVec3Eq(*local, Vec3(2.0F, 3.0F, 4.0F));
}

NOLINT_TEST_F(PhysicsModuleSyncTest, ParentScaleZeroComponentDoesNotProduceNaN)
{
  auto parent = scene_->CreateNode("parent-zero-scale");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(parent.GetTransform().SetLocalScale({ 0.0F, 2.0F, 2.0F }));

  auto child_opt = scene_->CreateChildNode(parent, "child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;
  ASSERT_TRUE(child.IsValid());
  scene_->Update();

  const auto body = AttachBody(child, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 10.0F, 5.0F, 2.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();
  const auto local = child.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  EXPECT_TRUE(std::isfinite(local->x));
  EXPECT_TRUE(std::isfinite(local->y));
  EXPECT_TRUE(std::isfinite(local->z));
}

NOLINT_TEST_F(PhysicsModuleSyncTest, ParentRotationConvertsWorldToLocal)
{
  auto parent = scene_->CreateNode("rot-parent");
  ASSERT_TRUE(parent.IsValid());
  const auto rot90z
    = glm::angleAxis(glm::half_pi<float>(), Vec3 { 0.0F, 0.0F, 1.0F });
  ASSERT_TRUE(parent.GetTransform().SetLocalRotation(rot90z));
  auto child_opt = scene_->CreateChildNode(parent, "rot-child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;
  ASSERT_TRUE(child.IsValid());
  scene_->Update();

  const auto body = AttachBody(child, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 0.0F, 1.0F, 0.0F },
      .rotation = rot90z,
    },
  };

  RunSceneMutation();
  const auto local = child.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  ExpectVec3Near(*local, Vec3 { 1.0F, 0.0F, 0.0F });
}

NOLINT_TEST_F(
  PhysicsModuleSyncTest, DeepHierarchyDynamicPullComputesLocalFromWorld)
{
  auto root = scene_->CreateNode("root");
  ASSERT_TRUE(root.IsValid());
  ASSERT_TRUE(root.GetTransform().SetLocalPosition({ 3.0F, 0.0F, 0.0F }));
  auto p1_opt = scene_->CreateChildNode(root, "p1");
  ASSERT_TRUE(p1_opt.has_value());
  auto p1 = *p1_opt;
  ASSERT_TRUE(p1.IsValid());
  ASSERT_TRUE(p1.GetTransform().SetLocalPosition({ 2.0F, 0.0F, 0.0F }));
  auto p2_opt = scene_->CreateChildNode(p1, "p2");
  ASSERT_TRUE(p2_opt.has_value());
  auto p2 = *p2_opt;
  ASSERT_TRUE(p2.IsValid());
  ASSERT_TRUE(p2.GetTransform().SetLocalPosition({ 1.0F, 0.0F, 0.0F }));
  scene_->Update();

  const auto body = AttachBody(p2, body::BodyType::kDynamic);
  ASSERT_TRUE(body.has_value());

  FakeState().active_transforms = {
    system::ActiveBodyTransform {
      .body_id = body->GetBodyId(),
      .position = Vec3 { 9.0F, 0.0F, 0.0F },
      .rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
    },
  };

  RunSceneMutation();
  const auto local = p2.GetTransform().GetLocalPosition();
  ASSERT_TRUE(local.has_value());
  ExpectVec3Eq(*local, Vec3 { 4.0F, 0.0F, 0.0F });
}

NOLINT_TEST_F(PhysicsModuleSyncTest, SceneMutationDrainsAndMapsPhysicsEvents)
{
  auto node_a = scene_->CreateNode("event-a");
  auto node_b = scene_->CreateNode("event-b");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(node_b.IsValid());
  const auto body_a = AttachBody(node_a, body::BodyType::kDynamic);
  const auto body_b = AttachBody(node_b, body::BodyType::kDynamic);
  ASSERT_TRUE(body_a.has_value());
  ASSERT_TRUE(body_b.has_value());

  FakeState().pending_events = {
    events::PhysicsEvent {
      .type = events::PhysicsEventType::kContactBegin,
      .body_a = body_a->GetBodyId(),
      .body_b = body_b->GetBodyId(),
    },
  };

  RunSceneMutation();
  const auto scene_events = module_->ConsumeSceneEvents();
  ASSERT_EQ(scene_events.size(), 1U);
  EXPECT_EQ(scene_events[0].type, events::PhysicsEventType::kContactBegin);
  ASSERT_TRUE(scene_events[0].node_a.has_value());
  ASSERT_TRUE(scene_events[0].node_b.has_value());
  EXPECT_EQ(scene_events[0].node_a.value(), node_a.GetHandle());
  EXPECT_EQ(scene_events[0].node_b.value(), node_b.GetHandle());

  const auto stats = module_->GetSyncDiagnostics();
  EXPECT_EQ(stats.event_drain_calls, 1U);
  EXPECT_EQ(stats.event_drain_count, 1U);
}

NOLINT_TEST_F(PhysicsModuleSyncTest, ConsumeSceneEventsDrainsBuffer)
{
  auto node_a = scene_->CreateNode("event-drain-a");
  auto node_b = scene_->CreateNode("event-drain-b");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(node_b.IsValid());
  const auto body_a = AttachBody(node_a, body::BodyType::kDynamic);
  const auto body_b = AttachBody(node_b, body::BodyType::kDynamic);
  ASSERT_TRUE(body_a.has_value());
  ASSERT_TRUE(body_b.has_value());

  FakeState().pending_events = {
    events::PhysicsEvent {
      .type = events::PhysicsEventType::kContactBegin,
      .body_a = body_a->GetBodyId(),
      .body_b = body_b->GetBodyId(),
    },
  };

  RunSceneMutation();
  const auto first = module_->ConsumeSceneEvents();
  ASSERT_EQ(first.size(), 1U);
  const auto second = module_->ConsumeSceneEvents();
  EXPECT_TRUE(second.empty());

  FakeState().pending_events = {
    events::PhysicsEvent {
      .type = events::PhysicsEventType::kContactEnd,
      .body_a = body_a->GetBodyId(),
      .body_b = body_b->GetBodyId(),
    },
  };
  RunSceneMutation();
  const auto third = module_->ConsumeSceneEvents();
  ASSERT_EQ(third.size(), 1U);
  EXPECT_EQ(third[0].type, events::PhysicsEventType::kContactEnd);
}

#if !defined(NDEBUG)
NOLINT_TEST_F(PhysicsModuleSyncTest, OnGameplayWrongPhase_Death)
{
  frame_.SetCurrentPhase(
    core::PhaseId::kSceneMutation, engine::internal::EngineTagFactory::Get());
  NOLINT_EXPECT_DEATH(co::Run(loop_,
                        [&]() -> co::Co<> {
                          co_await module_->OnGameplay(
                            observer_ptr<engine::FrameContext> { &frame_ });
                          co_return;
                        }),
    ".*");
}
#endif

#if !defined(NDEBUG)
NOLINT_TEST_F(PhysicsModuleSyncTest, OnSceneMutationWrongPhase_Death)
{
  frame_.SetCurrentPhase(
    core::PhaseId::kGameplay, engine::internal::EngineTagFactory::Get());
  NOLINT_EXPECT_DEATH(co::Run(loop_,
                        [&]() -> co::Co<> {
                          co_await module_->OnSceneMutation(
                            observer_ptr<engine::FrameContext> { &frame_ });
                          co_return;
                        }),
    ".*");
}
#endif

} // namespace oxygen::physics::test
