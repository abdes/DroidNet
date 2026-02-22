//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include "PhysicsModule_test_fixture.h"

namespace oxygen::physics::test {
namespace {

  class PhysicsModuleStressTest : public PhysicsModuleSyncTest { };

} // namespace

NOLINT_TEST_F(PhysicsModuleStressTest, BodyAttachDetachChurnOverManyFrames)
{
  constexpr size_t kFrames = 48;
  constexpr size_t kPerFrame = 8;

  for (size_t frame = 0; frame < kFrames; ++frame) {
    std::vector<scene::SceneNode> nodes {};
    nodes.reserve(kPerFrame);
    for (size_t i = 0; i < kPerFrame; ++i) {
      auto node = scene_->CreateNode("stress-body-node");
      ASSERT_TRUE(node.IsValid());
      ASSERT_TRUE(AttachBody(node, body::BodyType::kDynamic).has_value());
      nodes.push_back(node);
    }
    EXPECT_EQ(FakeState().bodies.size(), kPerFrame);

    for (auto& node : nodes) {
      ASSERT_TRUE(scene_->DestroyNode(node));
    }
    scene_->SyncObservers();
    RunSceneMutation();
    EXPECT_TRUE(FakeState().bodies.empty());
  }
}

NOLINT_TEST_F(PhysicsModuleStressTest, SceneSwitchBodyCharacterChurnCleansState)
{
  auto scene_a = scene_;
  auto scene_b = std::make_shared<scene::Scene>("PhysicsStressSceneB", 128);

  constexpr size_t kIterations = 24;
  for (size_t i = 0; i < kIterations; ++i) {
    {
      SetActiveScene(scene_a);
      RunGameplay();
      auto body_node = scene_a->CreateNode("stress-a-body");
      ASSERT_TRUE(body_node.IsValid());
      ASSERT_TRUE(AttachBody(body_node, body::BodyType::kDynamic).has_value());
      EXPECT_EQ(FakeState().bodies.size(), 1U);
    }

    {
      SetActiveScene(scene_b);
      RunGameplay();
      EXPECT_TRUE(FakeState().bodies.empty());
      EXPECT_TRUE(FakeState().characters.empty());

      auto char_node = scene_b->CreateNode("stress-b-character");
      ASSERT_TRUE(char_node.IsValid());
      scene_b->Update();
      ASSERT_TRUE(ScenePhysics::AttachCharacter(
        observer_ptr<PhysicsModule> { module_.get() }, char_node,
        character::CharacterDesc {})
          .has_value());
      EXPECT_EQ(FakeState().characters.size(), 1U);
    }
  }

  SetActiveScene(std::make_shared<scene::Scene>("PhysicsStressSceneFinal", 64));
  RunGameplay();
  EXPECT_TRUE(FakeState().bodies.empty());
  EXPECT_TRUE(FakeState().characters.empty());
}

NOLINT_TEST_F(PhysicsModuleStressTest, EventDrainChurnOverManyFrames)
{
  auto node_a = scene_->CreateNode("stress-event-a");
  auto node_b = scene_->CreateNode("stress-event-b");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(node_b.IsValid());

  const auto body_a = AttachBody(node_a, body::BodyType::kDynamic);
  const auto body_b = AttachBody(node_b, body::BodyType::kDynamic);
  ASSERT_TRUE(body_a.has_value());
  ASSERT_TRUE(body_b.has_value());

  constexpr size_t kFrames = 80;
  for (size_t frame = 0; frame < kFrames; ++frame) {
    const auto type = (frame % 2U) == 0U
      ? events::PhysicsEventType::kContactBegin
      : events::PhysicsEventType::kContactEnd;
    FakeState().pending_events = {
      events::PhysicsEvent {
        .type = type,
        .body_a = body_a->GetBodyId(),
        .body_b = body_b->GetBodyId(),
      },
    };

    RunSceneMutation();
    const auto drained = module_->ConsumeSceneEvents();
    ASSERT_EQ(drained.size(), 1U);
    EXPECT_EQ(drained[0].type, type);
    ASSERT_TRUE(drained[0].node_a.has_value());
    ASSERT_TRUE(drained[0].node_b.has_value());
    EXPECT_EQ(drained[0].node_a.value(), node_a.GetHandle());
    EXPECT_EQ(drained[0].node_b.value(), node_b.GetHandle());
    EXPECT_TRUE(module_->ConsumeSceneEvents().empty());
  }
}

} // namespace oxygen::physics::test
