//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
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
using oxygen::scene::internal::TransformMutation;
using oxygen::scene::testing_support::MutationObserver;
using oxygen::scene::testing_support::NoopScriptSlotProcessor;

class SceneTransformSyncTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_
      = oxygen::scene::testing_support::MakeScene("scene-transform-sync-test");
    node_ = scene_->CreateNode("transform-node");
    ASSERT_TRUE(node_.IsValid());
  }

  auto RegisterTransformObserver() -> void
  {
    ASSERT_TRUE(scene_->RegisterObserver(oxygen::observer_ptr { &observer_ },
      SceneMutationMask::kTransformChanged));
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

NOLINT_TEST_F(SceneTransformSyncTest, DeferredTransformDeliveredOnSync)
{
  RegisterTransformObserver();

  ASSERT_TRUE(node_.GetTransform().SetLocalPosition({ 1.0F, 2.0F, 3.0F }));
  EXPECT_TRUE(observer_.transform_changed.empty());

  scene_->SyncObservers();
  ASSERT_EQ(observer_.transform_changed.size(), 1U);
  EXPECT_EQ(observer_.transform_changed.front(), node_.GetHandle());
}

NOLINT_TEST_F(SceneTransformSyncTest, MaskIsolationWithMixedMutations)
{
  RegisterTransformObserver();

  auto light_node = NewNode("light-node");
  auto camera_node = NewNode("camera-node");
  ASSERT_TRUE(light_node.AttachLight(std::make_unique<DirectionalLight>()));
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));
  ASSERT_TRUE(node_.GetTransform().SetLocalPosition({ 3.0F, 2.0F, 1.0F }));

  scene_->SyncObservers();
  ASSERT_EQ(observer_.transform_changed.size(), 1U);
  EXPECT_EQ(observer_.transform_changed.front(), node_.GetHandle());
  EXPECT_TRUE(observer_.light_changed.empty());
  EXPECT_TRUE(observer_.camera_changed.empty());
  EXPECT_TRUE(observer_.node_destroyed.empty());
}

NOLINT_TEST_F(SceneTransformSyncTest, UnregisterBeforeSyncDropsQueuedMutations)
{
  RegisterTransformObserver();
  ASSERT_TRUE(node_.GetTransform().SetLocalPosition({ 4.0F, 5.0F, 6.0F }));

  ASSERT_TRUE(scene_->UnregisterObserver(oxygen::observer_ptr { &observer_ }));
  scene_->SyncObservers();
  EXPECT_TRUE(observer_.transform_changed.empty());
}

NOLINT_TEST_F(
  SceneTransformSyncTest, HydrationCollectionReplaysTransformAfterRegistration)
{
  scene_->CollectMutationsStart();
  ASSERT_TRUE(node_.GetTransform().SetLocalPosition({ 7.0F, 8.0F, 9.0F }));
  scene_->CollectMutationsEnd();

  RegisterTransformObserver();
  scene_->SyncObservers();

  ASSERT_EQ(observer_.transform_changed.size(), 1U);
  EXPECT_EQ(observer_.transform_changed.front(), node_.GetHandle());
}

NOLINT_TEST_F(SceneTransformSyncTest, NoBackfillWithoutHydrationCollection)
{
  ASSERT_TRUE(node_.GetTransform().SetLocalPosition({ 9.0F, 8.0F, 7.0F }));
  RegisterTransformObserver();
  scene_->SyncObservers();
  EXPECT_TRUE(observer_.transform_changed.empty());
}

NOLINT_TEST_F(SceneTransformSyncTest, EmptyFrameSyncLeavesNewCountersAtZero)
{
  RegisterTransformObserver();
  scene_->SyncObservers();
  const auto counters = scene_->GetMutationDispatchCounters();
  EXPECT_EQ(counters.sync_calls, 1U);
  EXPECT_EQ(counters.frames_with_mutations, 0U);
  EXPECT_EQ(counters.drained_records, 0U);
  EXPECT_EQ(counters.transform_records_coalesced_in, 0U);
  EXPECT_EQ(counters.transform_records_dispatched, 0U);
  EXPECT_EQ(counters.node_destroyed_records_dispatched, 0U);
}

class SceneTransformDispatcherTest : public testing::Test {
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

NOLINT_TEST_F(
  SceneTransformDispatcherTest, CoalescesTransformMutationsAndTracksCounters)
{
  const NodeHandle transform_node { 11, 1 };
  collector_->CollectTransformChanged(transform_node);
  collector_->CollectTransformChanged(transform_node);

  std::vector<TransformMutation> transform_notifications;
  dispatcher_->Dispatch(*collector_,
    {
      .resolve_script_slot = {},
      .notify_script_observers = {},
      .notify_light_mutation = {},
      .notify_camera_mutation = {},
      .notify_transform_mutation =
        [&transform_notifications](const TransformMutation& mutation) {
          transform_notifications.push_back(mutation);
        },
      .notify_node_destroyed_mutation = {},
    });

  ASSERT_EQ(transform_notifications.size(), 1U);
  EXPECT_EQ(transform_notifications.front().node_handle, transform_node);

  const auto counters = dispatcher_->GetCounters();
  EXPECT_EQ(counters.sync_calls, 1U);
  EXPECT_EQ(counters.frames_with_mutations, 1U);
  EXPECT_EQ(counters.drained_records, 2U);
  EXPECT_EQ(counters.transform_records_coalesced_in, 2U);
  EXPECT_EQ(counters.transform_records_dispatched, 1U);
}

} // namespace
