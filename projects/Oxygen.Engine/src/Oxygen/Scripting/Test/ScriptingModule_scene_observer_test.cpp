//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

namespace {

  using oxygen::co::testing::TestEventLoop;
  using oxygen::data::AssetKey;
  using oxygen::data::ScriptAsset;
  using oxygen::data::pak::ScriptAssetDesc;
  using oxygen::scene::Scene;
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

  auto MakeSceneWithReadySlot(const char* scene_name,
    const uint64_t script_hash,
    const size_t capacity = ScriptingModuleTest::kDefaultSceneCapacity)
    -> std::shared_ptr<Scene>
  {
    auto scene = std::make_shared<Scene>(scene_name, capacity);
    auto node = scene->CreateNode("script-node");
    if (!node.IsValid()) {
      return {};
    }
    if (!node.AttachScripting()) {
      return {};
    }
    auto scripting = node.GetScripting();
    if (!scripting.AddSlot(MakeScriptAsset())) {
      return {};
    }
    const auto slots = scripting.Slots();
    if (slots.empty()) {
      return {};
    }
    if (!scripting.MarkSlotReady(
          slots.front(), std::make_shared<const HashExecutable>(script_hash))) {
      return {};
    }

    return scene;
  }

} // namespace

NOLINT_TEST_F(
  ScriptingModuleTest, GameplaySceneSwitchAfterOldSceneDestroyedDoesNotCrash)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  TestEventLoop loop;

  auto scene_a = MakeSceneWithReadySlot("scene-a", 111);
  ASSERT_NE(scene_a, nullptr);
  engine::FrameContext frame_a;
  frame_a.SetScene(observer_ptr<scene::Scene> { scene_a.get() });

  oxygen::co::Run(loop, [&]() -> co::Co<> {
    co_await module.OnGameplay(observer_ptr<engine::FrameContext> { &frame_a });
    co_return;
  });

  // Reproduced crash path: old scene was destroyed before the next gameplay.
  scene_a.reset();

  auto scene_b = MakeSceneWithReadySlot("scene-b", 222);
  ASSERT_NE(scene_b, nullptr);
  engine::FrameContext frame_b;
  frame_b.SetScene(observer_ptr<scene::Scene> { scene_b.get() });

  EXPECT_NO_THROW(oxygen::co::Run(loop, [&]() -> co::Co<> {
    co_await module.OnGameplay(observer_ptr<engine::FrameContext> { &frame_b });
    co_return;
  }));

  module.OnShutdown();
}

} // namespace oxygen::scripting::test
