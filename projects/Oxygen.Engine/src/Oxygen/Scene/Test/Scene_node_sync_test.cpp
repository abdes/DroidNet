//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/MutationCollector.h>
#include <Oxygen/Scene/Internal/MutationDispatcher.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Test/Helpers/SceneMutationTestSupport.h>

namespace {

using oxygen::observer_ptr;
using oxygen::scene::DirectionalLight;
using oxygen::scene::NodeHandle;
using oxygen::scene::PerspectiveCamera;
using oxygen::scene::Scene;
using oxygen::scene::SceneMutationMask;
using oxygen::scene::internal::NodeDestroyedMutation;
using oxygen::scene::testing_support::MutationObserver;
using oxygen::scene::testing_support::NoopScriptSlotProcessor;

class SceneNodeSyncTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_ = oxygen::scene::testing_support::MakeScene("scene-node-sync-test");
    node_ = scene_->CreateNode("destroyed-node");
    ASSERT_TRUE(node_.IsValid());
  }

  auto RegisterNodeDestroyedObserver() -> void
  {
    ASSERT_TRUE(scene_->RegisterObserver(
      oxygen::observer_ptr { &observer_ }, SceneMutationMask::kNodeDestroyed));
  }

  auto NewNode(const char* name) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(name);
    EXPECT_TRUE(node.IsValid());
    return node;
  }

  std::shared_ptr<Scene> scene_;
  oxygen::scene::SceneNode node_;
  MutationObserver observer_ {};
};

NOLINT_TEST_F(SceneNodeSyncTest, DeferredNodeDestroyedDeliveredOnSync)
{
  RegisterNodeDestroyedObserver();

  const auto handle = node_.GetHandle();
  ASSERT_TRUE(scene_->DestroyNode(node_));
  EXPECT_TRUE(observer_.node_destroyed.empty());

  scene_->SyncObservers();
  ASSERT_EQ(observer_.node_destroyed.size(), 1U);
  EXPECT_EQ(observer_.node_destroyed.front(), handle);
}

NOLINT_TEST_F(SceneNodeSyncTest, HierarchyDestroyEmitsOneRecordPerNodePostOrder)
{
  auto root = NewNode("root");
  auto child_opt = scene_->CreateChildNode(root, "child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;
  auto grandchild_opt = scene_->CreateChildNode(child, "grandchild");
  ASSERT_TRUE(grandchild_opt.has_value());
  auto grandchild = *grandchild_opt;

  RegisterNodeDestroyedObserver();
  const auto root_handle = root.GetHandle();
  const auto child_handle = child.GetHandle();
  const auto grandchild_handle = grandchild.GetHandle();

  ASSERT_TRUE(scene_->DestroyNodeHierarchy(root));
  scene_->SyncObservers();

  ASSERT_EQ(observer_.node_destroyed.size(), 3U);
  EXPECT_EQ(observer_.node_destroyed[0], grandchild_handle);
  EXPECT_EQ(observer_.node_destroyed[1], child_handle);
  EXPECT_EQ(observer_.node_destroyed[2], root_handle);
}

NOLINT_TEST_F(SceneNodeSyncTest, MaskIsolationWithMixedMutations)
{
  RegisterNodeDestroyedObserver();

  auto transform_node = NewNode("transform-node");
  auto light_node = NewNode("light-node");
  auto camera_node = NewNode("camera-node");
  ASSERT_TRUE(
    transform_node.GetTransform().SetLocalPosition({ 1.0F, 1.0F, 1.0F }));
  ASSERT_TRUE(light_node.AttachLight(std::make_unique<DirectionalLight>()));
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));
  ASSERT_TRUE(scene_->DestroyNode(node_));

  scene_->SyncObservers();
  ASSERT_EQ(observer_.node_destroyed.size(), 1U);
  EXPECT_TRUE(observer_.transform_changed.empty());
  EXPECT_TRUE(observer_.light_changed.empty());
  EXPECT_TRUE(observer_.camera_changed.empty());
}

NOLINT_TEST_F(SceneNodeSyncTest, UnregisterBeforeSyncDropsQueuedMutations)
{
  RegisterNodeDestroyedObserver();
  ASSERT_TRUE(scene_->DestroyNode(node_));

  ASSERT_TRUE(scene_->UnregisterObserver(oxygen::observer_ptr { &observer_ }));
  scene_->SyncObservers();
  EXPECT_TRUE(observer_.node_destroyed.empty());
}

NOLINT_TEST_F(
  SceneNodeSyncTest, HydrationCollectionReplaysDestroyAfterRegistration)
{
  const auto handle = node_.GetHandle();
  scene_->CollectMutationsStart();
  ASSERT_TRUE(scene_->DestroyNode(node_));
  scene_->CollectMutationsEnd();

  RegisterNodeDestroyedObserver();
  scene_->SyncObservers();
  ASSERT_EQ(observer_.node_destroyed.size(), 1U);
  EXPECT_EQ(observer_.node_destroyed.front(), handle);
}

NOLINT_TEST_F(SceneNodeSyncTest, NoBackfillWithoutHydrationCollection)
{
  ASSERT_TRUE(scene_->DestroyNode(node_));
  RegisterNodeDestroyedObserver();
  scene_->SyncObservers();
  EXPECT_TRUE(observer_.node_destroyed.empty());
}

NOLINT_TEST_F(
  SceneNodeSyncTest, TransformThenDestroySameFrameDispatchesDestroyFirst)
{
  class OrderedMutationObserver final : public oxygen::scene::ISceneObserver {
  public:
    auto OnTransformChanged(const NodeHandle& node_handle) noexcept
      -> void override
    {
      ordered_events.emplace_back('T', node_handle);
    }

    auto OnNodeDestroyed(const NodeHandle& node_handle) noexcept
      -> void override
    {
      ordered_events.emplace_back('D', node_handle);
    }

    std::vector<std::pair<char, NodeHandle>> ordered_events {};
  };

  OrderedMutationObserver ordered_observer;
  auto moving_node = NewNode("moving-node");
  ASSERT_TRUE(
    scene_->RegisterObserver(oxygen::observer_ptr { &ordered_observer }));

  ASSERT_TRUE(
    moving_node.GetTransform().SetLocalPosition({ 2.0F, 3.0F, 4.0F }));
  const auto handle = moving_node.GetHandle();
  ASSERT_TRUE(scene_->DestroyNode(moving_node));
  scene_->SyncObservers();

  ASSERT_EQ(ordered_observer.ordered_events.size(), 2U);
  // Dispatcher contract:
  // - node-destroyed is dispatched during drain
  // - transform is coalesced and flushed at end-of-sync
  EXPECT_EQ(ordered_observer.ordered_events[0].first, 'D');
  EXPECT_EQ(ordered_observer.ordered_events[0].second, handle);
  EXPECT_EQ(ordered_observer.ordered_events[1].first, 'T');
  EXPECT_EQ(ordered_observer.ordered_events[1].second, handle);
}

class SceneNodeDispatcherTest : public testing::Test {
protected:
  void SetUp() override
  {
    collector_ = oxygen::scene::internal::CreateMutationCollector();
    ASSERT_NE(collector_, nullptr);

    dispatcher_ = oxygen::scene::internal::CreateMutationDispatcher(
      observer_ptr { &script_processor_ });
    ASSERT_NE(dispatcher_, nullptr);
  }

  std::unique_ptr<oxygen::scene::internal::IMutationCollector> collector_;
  std::unique_ptr<oxygen::scene::internal::IMutationDispatcher> dispatcher_;
  NoopScriptSlotProcessor script_processor_ {};
};

NOLINT_TEST_F(SceneNodeDispatcherTest, DispatchesNodeDestroyedAndTracksCounters)
{
  const NodeHandle node_destroyed { 12, 1 };
  collector_->CollectNodeDestroyed(node_destroyed);

  std::vector<NodeDestroyedMutation> destroyed_notifications;
  dispatcher_->Dispatch(*collector_,
    {
      .resolve_script_slot = {},
      .notify_script_observers = {},
      .notify_light_mutation = {},
      .notify_camera_mutation = {},
      .notify_transform_mutation = {},
      .notify_node_destroyed_mutation =
        [&destroyed_notifications](const NodeDestroyedMutation& mutation) {
          destroyed_notifications.push_back(mutation);
        },
    });

  ASSERT_EQ(destroyed_notifications.size(), 1U);
  EXPECT_EQ(destroyed_notifications.front().node_handle, node_destroyed);

  const auto counters = dispatcher_->GetCounters();
  EXPECT_EQ(counters.sync_calls, 1U);
  EXPECT_EQ(counters.frames_with_mutations, 1U);
  EXPECT_EQ(counters.drained_records, 1U);
  EXPECT_EQ(counters.node_destroyed_records_dispatched, 1U);
}

NOLINT_TEST_F(SceneNodeDispatcherTest, PreservesNodeDestroyedRecordOrder)
{
  const NodeHandle first { 13, 1 };
  const NodeHandle second { 14, 1 };
  collector_->CollectNodeDestroyed(first);
  collector_->CollectNodeDestroyed(second);

  std::vector<NodeDestroyedMutation> destroyed_notifications;
  dispatcher_->Dispatch(*collector_,
    {
      .resolve_script_slot = {},
      .notify_script_observers = {},
      .notify_light_mutation = {},
      .notify_camera_mutation = {},
      .notify_transform_mutation = {},
      .notify_node_destroyed_mutation =
        [&destroyed_notifications](const NodeDestroyedMutation& mutation) {
          destroyed_notifications.push_back(mutation);
        },
    });

  ASSERT_EQ(destroyed_notifications.size(), 2U);
  EXPECT_EQ(destroyed_notifications[0].node_handle, first);
  EXPECT_EQ(destroyed_notifications[1].node_handle, second);
}

} // namespace
