//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

namespace {

  using oxygen::co::testing::TestEventLoop;
  using oxygen::core::meta::scripting::ScriptCompileMode;
  using oxygen::data::AssetKey;
  using oxygen::data::ScriptAsset;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::scene::Scene;
  using oxygen::scripting::CompiledScriptExecutable;
  using oxygen::scripting::LuauScriptCompiler;
  using oxygen::scripting::ScriptBlobCanonicalName;
  using oxygen::scripting::ScriptBlobOrigin;
  using oxygen::scripting::ScriptExecutable;
  using oxygen::scripting::ScriptSourceBlob;

  auto ToBytes(const std::string_view text) -> std::vector<uint8_t>
  {
    std::vector<uint8_t> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
      bytes.push_back(static_cast<uint8_t>(ch));
    }
    return bytes;
  }

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

  auto MakeSceneWithExecutableSlot(const char* scene_name,
    std::shared_ptr<const ScriptExecutable> executable,
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
    if (!scripting.MarkSlotReady(slots.front(), std::move(executable))) {
      return {};
    }

    return scene;
  }

  auto MakeSceneWithReadySlot(const char* scene_name,
    const uint64_t script_hash,
    const size_t capacity = ScriptingModuleTest::kDefaultSceneCapacity)
    -> std::shared_ptr<Scene>
  {
    return MakeSceneWithExecutableSlot(scene_name,
      std::make_shared<const HashExecutable>(script_hash), capacity);
  }

  auto CompileSlotExecutable(const std::string_view source,
    const uint64_t content_hash) -> std::shared_ptr<const ScriptExecutable>
  {
    LuauScriptCompiler compiler;
    auto compile_result = compiler.Compile(
      ScriptSourceBlob::FromOwned(ToBytes(source),
        oxygen::data::pak::scripting::ScriptLanguage::kLuau,
        oxygen::data::pak::scripting::ScriptCompression::kNone, content_hash,
        ScriptBlobOrigin::kEmbeddedResource,
        ScriptBlobCanonicalName { "scene_slot_local_state" }),
      ScriptCompileMode::kDebug);
    if (!compile_result.success || !compile_result.HasBytecode()) {
      return {};
    }
    return std::make_shared<const CompiledScriptExecutable>(
      compile_result.bytecode);
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

NOLINT_TEST_F(
  ScriptingModuleTest, SceneSlotLocalStatePersistsAcrossGameplayFrames)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  constexpr std::string_view kSlotScript = R"lua(
local counter = 0
local script = {}

function script.on_gameplay(_ctx, _dt_seconds)
  counter = counter + 1
  _G.__slot_counter = counter
end

return script
)lua";

  auto slot_executable = CompileSlotExecutable(kSlotScript, 0x33445566ULL);
  ASSERT_NE(slot_executable, nullptr);

  auto scene = MakeSceneWithExecutableSlot(
    "scene-local-state", std::move(slot_executable));
  ASSERT_NE(scene, nullptr);

  engine::FrameContext frame;
  frame.SetScene(observer_ptr<scene::Scene> { scene.get() });
  TestEventLoop loop;

  oxygen::co::Run(loop, [&]() -> co::Co<> {
    co_await module.OnGameplay(observer_ptr<engine::FrameContext> { &frame });
    co_return;
  });
  oxygen::co::Run(loop, [&]() -> co::Co<> {
    co_await module.OnGameplay(observer_ptr<engine::FrameContext> { &frame });
    co_return;
  });

  const auto verify_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if _G.__slot_counter ~= 2 then
  error("expected __slot_counter == 2, got " .. tostring(_G.__slot_counter))
end
)lua" },
    .chunk_name = ScriptChunkName { "verify_slot_local_state_persistence" },
  });
  EXPECT_TRUE(verify_result.ok)
    << verify_result.stage << ": " << verify_result.message;

  module.OnShutdown();
}

} // namespace oxygen::scripting::test
