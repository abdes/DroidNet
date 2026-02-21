//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::ScriptAsset;
using oxygen::data::pak::ScriptAssetDesc;
using oxygen::scene::DirectionalLight;
using oxygen::scene::ISceneObserver;
using oxygen::scene::NodeHandle;
using oxygen::scene::PerspectiveCamera;
using oxygen::scene::Scene;
using oxygen::scene::SceneMutationMask;
using oxygen::scene::ScriptingComponent;
using oxygen::scripting::ScriptExecutable;

auto MakeScriptAsset() -> std::shared_ptr<const ScriptAsset>
{
  ScriptAssetDesc desc {};
  return std::make_shared<ScriptAsset>(AssetKey {}, desc);
}

class HashExecutable final : public ScriptExecutable {
public:
  explicit HashExecutable(const uint64_t hash) noexcept
    : hash_(hash)
  {
  }

  auto Run() const noexcept -> void override { }

  [[nodiscard]] auto ContentHash() const noexcept -> uint64_t override
  {
    return hash_;
  }

private:
  uint64_t hash_ { 0 };
};

struct ScriptSlotEvent final {
  NodeHandle node_handle;
  oxygen::scene::ScriptSlotIndex slot_index {};
};

class TestSceneObserver final : public ISceneObserver {
public:
  auto OnScriptSlotActivated(const NodeHandle& node_handle,
    const oxygen::scene::ScriptSlotIndex slot_index,
    const ScriptingComponent::Slot& /*slot*/) noexcept -> void override
  {
    activated.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  auto OnScriptSlotChanged(const NodeHandle& node_handle,
    const oxygen::scene::ScriptSlotIndex slot_index,
    const ScriptingComponent::Slot& /*slot*/) noexcept -> void override
  {
    changed.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  auto OnScriptSlotDeactivated(const NodeHandle& node_handle,
    const oxygen::scene::ScriptSlotIndex slot_index) noexcept -> void override
  {
    deactivated.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  auto OnLightChanged(const NodeHandle& node_handle) noexcept -> void override
  {
    light_changed.push_back(node_handle);
  }

  auto OnCameraChanged(const NodeHandle& node_handle) noexcept -> void override
  {
    camera_changed.push_back(node_handle);
  }

  std::vector<ScriptSlotEvent> activated {};
  std::vector<ScriptSlotEvent> changed {};
  std::vector<ScriptSlotEvent> deactivated {};
  std::vector<NodeHandle> light_changed {};
  std::vector<NodeHandle> camera_changed {};
};

auto BuildSceneWithScriptSlot()
  -> std::pair<std::shared_ptr<Scene>, oxygen::scene::SceneNode>
{
  constexpr std::size_t kTestSceneCapacity = 100;
  auto scene
    = std::make_shared<Scene>("observer-sync-test", kTestSceneCapacity);
  auto node = scene->CreateNode("script-node");
  if (!node.IsValid()) {
    return { {}, {} };
  }
  if (!node.AttachScripting()) {
    return { {}, {} };
  }

  auto scripting = node.GetScripting();
  if (!scripting.AddSlot(MakeScriptAsset())) {
    return { {}, {} };
  }
  const auto slots = scripting.Slots();
  if (slots.empty()) {
    return { {}, {} };
  }
  return { std::move(scene), node };
}

NOLINT_TEST(
  SceneMutationObserverSyncTest, FirstSyncActivatesAndSecondSyncIsStable)
{
  auto [scene, node] = BuildSceneWithScriptSlot();
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));

  auto scripting = node.GetScripting();
  const auto slots = scripting.Slots();
  ASSERT_EQ(slots.size(), 1U);
  ASSERT_TRUE(scripting.MarkSlotReady(
    slots.front(), std::make_shared<const HashExecutable>(1001)));

  scene->SyncObservers();
  ASSERT_EQ(observer.activated.size(), 1U);
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());
  const auto counters_after_first_sync = scene->GetMutationDispatchCounters();
  EXPECT_EQ(counters_after_first_sync.sync_calls, 1U);
  EXPECT_EQ(counters_after_first_sync.frames_with_mutations, 1U);
  EXPECT_EQ(counters_after_first_sync.drained_records, 1U);
  EXPECT_EQ(counters_after_first_sync.script_records_dispatched, 1U);
  EXPECT_EQ(counters_after_first_sync.light_records_coalesced_in, 0U);
  EXPECT_EQ(counters_after_first_sync.light_records_dispatched, 0U);
  EXPECT_EQ(counters_after_first_sync.camera_records_coalesced_in, 0U);
  EXPECT_EQ(counters_after_first_sync.camera_records_dispatched, 0U);

  scene->SyncObservers();
  EXPECT_EQ(observer.activated.size(), 1U);
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());
  const auto counters_after_second_sync = scene->GetMutationDispatchCounters();
  EXPECT_EQ(counters_after_second_sync.sync_calls, 2U);
  EXPECT_EQ(counters_after_second_sync.frames_with_mutations, 1U);
  EXPECT_EQ(counters_after_second_sync.drained_records, 1U);
  EXPECT_EQ(counters_after_second_sync.script_records_dispatched, 1U);
  EXPECT_TRUE(observer.light_changed.empty());
  EXPECT_TRUE(observer.camera_changed.empty());
}

NOLINT_TEST(SceneMutationObserverSyncTest, ChangedExecutableEmitsChangedEvent)
{
  auto [scene, node] = BuildSceneWithScriptSlot();
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));

  auto scripting = node.GetScripting();
  const auto slots = scripting.Slots();
  ASSERT_EQ(slots.size(), 1U);
  ASSERT_TRUE(scripting.MarkSlotReady(
    slots.front(), std::make_shared<const HashExecutable>(1001)));
  scene->SyncObservers();

  const auto new_executable = std::make_shared<const HashExecutable>(2002);
  ASSERT_TRUE(scripting.MarkSlotReady(slots.front(), new_executable));

  scene->SyncObservers();
  ASSERT_EQ(observer.changed.size(), 1U);
  EXPECT_EQ(observer.changed.front().node_handle, node.GetHandle());
  EXPECT_EQ(observer.changed.front().slot_index.get(), 0U);
}

NOLINT_TEST(SceneMutationObserverSyncTest, RemovedSlotEmitsDeactivatedEvent)
{
  auto [scene, node] = BuildSceneWithScriptSlot();
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));

  auto scripting = node.GetScripting();
  const auto slots = scripting.Slots();
  ASSERT_EQ(slots.size(), 1U);
  ASSERT_TRUE(scripting.MarkSlotReady(
    slots.front(), std::make_shared<const HashExecutable>(1001)));
  scene->SyncObservers();

  ASSERT_TRUE(scripting.RemoveSlot(slots.front()));

  scene->SyncObservers();
  ASSERT_EQ(observer.deactivated.size(), 1U);
  EXPECT_EQ(observer.deactivated.front().node_handle, node.GetHandle());
  EXPECT_EQ(observer.deactivated.front().slot_index.get(), 0U);
}

NOLINT_TEST(
  SceneMutationObserverSyncTest, LightObserverReceivesFutureMutationsOnly)
{
  constexpr std::size_t kTestSceneCapacity = 100;
  auto scene
    = std::make_shared<Scene>("observer-sync-light-test", kTestSceneCapacity);
  auto node = scene->CreateNode("light-node");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(node.AttachLight(std::make_unique<DirectionalLight>()));
  scene->SyncObservers();
  const auto counters_without_observer = scene->GetMutationDispatchCounters();
  EXPECT_EQ(counters_without_observer.sync_calls, 0U);
  EXPECT_EQ(counters_without_observer.drained_records, 0U);

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(
    oxygen::observer_ptr { &observer }, SceneMutationMask::kLightChanged));

  scene->SyncObservers();
  EXPECT_TRUE(observer.light_changed.empty());

  ASSERT_TRUE(node.ReplaceLight(std::make_unique<DirectionalLight>()));
  scene->SyncObservers();
  ASSERT_EQ(observer.light_changed.size(), 1U);
  EXPECT_EQ(observer.light_changed.front(), node.GetHandle());
  EXPECT_TRUE(observer.activated.empty());
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());
  EXPECT_TRUE(observer.camera_changed.empty());
}

NOLINT_TEST(
  SceneMutationObserverSyncTest, CameraObserverReceivesFutureMutationsOnly)
{
  constexpr std::size_t kTestSceneCapacity = 100;
  auto scene
    = std::make_shared<Scene>("observer-sync-camera-test", kTestSceneCapacity);
  auto node = scene->CreateNode("camera-node");
  ASSERT_TRUE(node.IsValid());
  ASSERT_TRUE(node.AttachCamera(std::make_unique<PerspectiveCamera>()));
  scene->SyncObservers();
  const auto counters_without_observer = scene->GetMutationDispatchCounters();
  EXPECT_EQ(counters_without_observer.sync_calls, 0U);
  EXPECT_EQ(counters_without_observer.drained_records, 0U);

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(
    oxygen::observer_ptr { &observer }, SceneMutationMask::kCameraChanged));

  scene->SyncObservers();
  EXPECT_TRUE(observer.camera_changed.empty());

  ASSERT_TRUE(node.ReplaceCamera(std::make_unique<PerspectiveCamera>()));
  scene->SyncObservers();
  ASSERT_EQ(observer.camera_changed.size(), 1U);
  EXPECT_EQ(observer.camera_changed.front(), node.GetHandle());
  EXPECT_TRUE(observer.activated.empty());
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());
  EXPECT_TRUE(observer.light_changed.empty());
}

} // namespace
