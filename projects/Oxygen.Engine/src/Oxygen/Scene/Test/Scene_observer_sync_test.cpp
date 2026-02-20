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
#include <Oxygen/Scene/Scene.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::ScriptAsset;
using oxygen::data::pak::ScriptAssetDesc;
using oxygen::scene::ISceneObserver;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
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
  uint32_t slot_index { 0 };
};

class TestSceneObserver final : public ISceneObserver {
public:
  auto OnScriptSlotActivated(const NodeHandle& node_handle,
    const uint32_t slot_index,
    const ScriptingComponent::Slot& /*slot*/) noexcept -> void override
  {
    activated.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  auto OnScriptSlotChanged(const NodeHandle& node_handle,
    const uint32_t slot_index,
    const ScriptingComponent::Slot& /*slot*/) noexcept -> void override
  {
    changed.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  auto OnScriptSlotDeactivated(const NodeHandle& node_handle,
    const uint32_t slot_index) noexcept -> void override
  {
    deactivated.push_back(ScriptSlotEvent {
      .node_handle = node_handle,
      .slot_index = slot_index,
    });
  }

  std::vector<ScriptSlotEvent> activated {};
  std::vector<ScriptSlotEvent> changed {};
  std::vector<ScriptSlotEvent> deactivated {};
};

auto BuildSceneWithReadyScript(const uint64_t script_hash)
  -> std::pair<std::shared_ptr<Scene>, oxygen::scene::SceneNode>
{
  auto scene = std::make_shared<Scene>("observer-sync-test");
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
  const auto executable = std::make_shared<const HashExecutable>(script_hash);
  if (!scripting.MarkSlotReady(slots.front(), executable)) {
    return { {}, {} };
  }

  return { std::move(scene), node };
}

NOLINT_TEST(Scene_observer_sync_test, FirstSyncActivatesAndSecondSyncIsStable)
{
  auto [scene, node] = BuildSceneWithReadyScript(1001);
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));

  scene->SyncObservers();
  ASSERT_EQ(observer.activated.size(), 1U);
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());

  scene->SyncObservers();
  EXPECT_EQ(observer.activated.size(), 1U);
  EXPECT_TRUE(observer.changed.empty());
  EXPECT_TRUE(observer.deactivated.empty());
}

NOLINT_TEST(Scene_observer_sync_test, ChangedExecutableEmitsChangedEvent)
{
  auto [scene, node] = BuildSceneWithReadyScript(1001);
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));
  scene->SyncObservers();

  auto scripting = node.GetScripting();
  const auto slots = scripting.Slots();
  ASSERT_EQ(slots.size(), 1U);
  const auto new_executable = std::make_shared<const HashExecutable>(2002);
  ASSERT_TRUE(scripting.MarkSlotReady(slots.front(), new_executable));

  scene->SyncObservers();
  ASSERT_EQ(observer.changed.size(), 1U);
  EXPECT_EQ(observer.changed.front().node_handle, node.GetHandle());
  EXPECT_EQ(observer.changed.front().slot_index, 0U);
}

NOLINT_TEST(Scene_observer_sync_test, RemovedSlotEmitsDeactivatedEvent)
{
  auto [scene, node] = BuildSceneWithReadyScript(1001);
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(node.IsValid());

  TestSceneObserver observer;
  ASSERT_TRUE(scene->RegisterObserver(oxygen::observer_ptr { &observer }));
  scene->SyncObservers();

  auto scripting = node.GetScripting();
  const auto slots = scripting.Slots();
  ASSERT_EQ(slots.size(), 1U);
  ASSERT_TRUE(scripting.RemoveSlot(slots.front()));

  scene->SyncObservers();
  ASSERT_EQ(observer.deactivated.size(), 1U);
  EXPECT_EQ(observer.deactivated.front().node_handle, node.GetHandle());
  EXPECT_EQ(observer.deactivated.front().slot_index, 0U);
}

} // namespace
