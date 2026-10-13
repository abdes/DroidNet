//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::observer_ptr;
using oxygen::host::SceneStage;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneMutationMask;

constexpr std::size_t kSceneCapacity = 16U;

class TransformObserver final : public oxygen::scene::ISceneObserver {
public:
  auto OnTransformChanged(const NodeHandle& node_handle) noexcept
    -> void override
  {
    changed.push_back(node_handle);
  }

  std::vector<NodeHandle> changed;
};

auto MakeScene(const char* name) -> std::shared_ptr<Scene>
{
  return std::make_shared<Scene>(name, kSceneCapacity);
}

class SceneStageTest : public testing::Test {
protected:
  SceneStage stage_;
};

//! A new stage has no scene and generation zero.
NOLINT_TEST_F(SceneStageTest, StartsEmpty)
{
  EXPECT_EQ(stage_.TryGetScene(), nullptr);
  EXPECT_FALSE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.GetGeneration(), 0U);
  EXPECT_FALSE(stage_.GetActiveScene().IsValid());
  EXPECT_FALSE(stage_.PublishStaged());
}

//! A staged scene becomes active only when published.
NOLINT_TEST_F(SceneStageTest, StagedSceneBecomesActiveOnPublish)
{
  const auto scene = MakeScene("staged");
  stage_.Stage(scene);

  ASSERT_TRUE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.GetStagedScene().get(), scene.get());
  EXPECT_EQ(stage_.TryGetScene(), nullptr);
  EXPECT_EQ(stage_.GetGeneration(), 0U);

  ASSERT_TRUE(stage_.PublishStaged());
  EXPECT_EQ(stage_.TryGetScene().get(), scene.get());
  EXPECT_FALSE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.GetGeneration(), 1U);
  EXPECT_FALSE(stage_.PublishStaged());
  EXPECT_EQ(stage_.GetGeneration(), 1U);
}

//! Staging again replaces the staged scene.
NOLINT_TEST_F(SceneStageTest, StageReplacesStagedScene)
{
  stage_.Stage(MakeScene("first"));
  const auto second = MakeScene("second");
  stage_.Stage(second);

  ASSERT_TRUE(stage_.PublishStaged());
  EXPECT_EQ(stage_.TryGetScene().get(), second.get());
}

//! The camera selected for a staged scene is handed over once, on publish.
NOLINT_TEST_F(SceneStageTest, PublishHandsOverMainCamera)
{
  const auto scene = MakeScene("camera");
  auto camera = scene->CreateNode("camera");
  stage_.Stage(scene);
  stage_.SetStagedMainCamera(camera);

  EXPECT_FALSE(stage_.TakePublishedMainCamera().IsAlive());
  ASSERT_TRUE(stage_.PublishStaged());
  const auto published = stage_.TakePublishedMainCamera();
  ASSERT_TRUE(published.IsAlive());
  EXPECT_EQ(published.GetHandle(), camera.GetHandle());
  EXPECT_FALSE(stage_.TakePublishedMainCamera().IsAlive());
}

//! Discarding drops the staged scene and leaves the active one.
NOLINT_TEST_F(SceneStageTest, DiscardKeepsActiveScene)
{
  const auto active = MakeScene("active");
  stage_.Stage(active);
  ASSERT_TRUE(stage_.PublishStaged());
  stage_.Stage(MakeScene("discarded"));

  stage_.DiscardStagedScene();

  EXPECT_FALSE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.TryGetScene().get(), active.get());
  EXPECT_EQ(stage_.GetGeneration(), 1U);
}

//! Clear drops the active and the staged scenes and advances the generation.
NOLINT_TEST_F(SceneStageTest, ClearDropsActiveAndStagedScenes)
{
  stage_.Stage(MakeScene("active"));
  ASSERT_TRUE(stage_.PublishStaged());
  stage_.Stage(MakeScene("staged"));

  stage_.Clear();

  EXPECT_EQ(stage_.TryGetScene(), nullptr);
  EXPECT_FALSE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.GetGeneration(), 2U);
}

//! SetScene makes a scene active directly and drops any staged scene.
NOLINT_TEST_F(SceneStageTest, SetSceneReplacesActiveAndDropsStaged)
{
  stage_.Stage(MakeScene("staged"));
  const auto direct = MakeScene("direct");

  stage_.SetScene(direct);

  EXPECT_EQ(stage_.TryGetScene().get(), direct.get());
  EXPECT_FALSE(stage_.HasStagedScene());
  EXPECT_EQ(stage_.GetGeneration(), 1U);
}

//! A scene handle goes stale when the active scene changes.
NOLINT_TEST_F(SceneStageTest, ActiveSceneHandleGoesStaleOnChange)
{
  const auto first = MakeScene("first");
  stage_.SetScene(first);
  const auto handle = stage_.GetActiveScene();
  ASSERT_TRUE(handle.IsValid());
  EXPECT_EQ(handle.operator->(), first.get());

  stage_.Stage(MakeScene("second"));
  EXPECT_TRUE(handle.IsValid());
  ASSERT_TRUE(stage_.PublishStaged());
  EXPECT_FALSE(handle.IsValid());
  EXPECT_TRUE(stage_.GetActiveScene().IsValid());
}

//! Mutations made while a scene is staged reach an observer that registers
//! after the scene is published.
NOLINT_TEST_F(SceneStageTest, ReplaysMutationsMadeWhileStaged)
{
  const auto scene = MakeScene("hydrated");
  auto built = scene->CreateNode("built");
  stage_.Stage(scene);
  ASSERT_TRUE(built.GetTransform().SetLocalPosition({ 1.0F, 2.0F, 3.0F }));
  ASSERT_TRUE(stage_.PublishStaged());

  TransformObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(
    observer_ptr { &observer }, SceneMutationMask::kTransformChanged));
  scene->SyncObservers();

  ASSERT_EQ(observer.changed.size(), 1U);
  EXPECT_EQ(observer.changed.front(), built.GetHandle());
}

//! A scene made active without staging replays nothing to a late observer.
NOLINT_TEST_F(SceneStageTest, SetSceneReplaysNoEarlierMutations)
{
  const auto scene = MakeScene("direct");
  auto node = scene->CreateNode("node");
  stage_.SetScene(scene);
  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 1.0F, 1.0F, 1.0F }));

  TransformObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(
    observer_ptr { &observer }, SceneMutationMask::kTransformChanged));
  scene->SyncObservers();

  EXPECT_TRUE(observer.changed.empty());
}

} // namespace
