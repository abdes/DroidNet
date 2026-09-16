//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <lua.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
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

  auto CompileSlotBytecode(const std::string_view source,
    const uint64_t content_hash) -> std::shared_ptr<const ScriptBytecodeBlob>
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
    if (content_hash == 0) {
      // Preserve the compiler's unstamped output for runtime reload coverage.
      return compile_result.bytecode;
    }
    const auto bytes = compile_result.bytecode->BytesView();
    return std::make_shared<const ScriptBytecodeBlob>(
      ScriptBytecodeBlob::FromOwned(
        std::vector<uint8_t>(bytes.begin(), bytes.end()),
        compile_result.bytecode->Language(),
        compile_result.bytecode->Compression(), content_hash,
        ScriptBlobOrigin::kEmbeddedResource,
        ScriptBlobCanonicalName { "scene_slot_local_state" }));
  }

  auto CompileSlotExecutable(const std::string_view source,
    const uint64_t content_hash) -> std::shared_ptr<const ScriptExecutable>
  {
    auto bytecode = CompileSlotBytecode(source, content_hash);
    if (!bytecode) {
      return {};
    }
    return std::make_shared<const CompiledScriptExecutable>(
      std::move(bytecode));
  }

  auto RunSlotHooks(ScriptingModule& module, engine::FrameContext& frame)
    -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(loop, module.OnGameplay(observer_ptr { &frame }));
    oxygen::co::Run(loop, module.OnSceneMutation(observer_ptr { &frame }));
  }

  struct ReattachFixture {
    scene::SceneNode node;
    std::shared_ptr<const ScriptAsset> asset;
    std::shared_ptr<const ScriptExecutable> executable;
  };

  class ReattachBindingPack final
    : public bindings::contracts::IScriptBindingPack {
  public:
    explicit ReattachBindingPack(std::shared_ptr<ReattachFixture> fixture)
      : fixture_(std::move(fixture))
    {
    }

    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "test.script-reattach";
    }

    [[nodiscard]] auto Register(
      const bindings::contracts::ScriptBindingPackContext& context) const
      -> bool override
    {
      auto* state = context.lua_state;
      lua_getref(state, context.runtime_env_ref);
      lua_getfield(state, -1, "oxygen");
      lua_getfield(state, -1, "events");
      // This test-only closure is owned by the module's binding pack lifetime;
      // it reuses the exact executable object, which Lua authoring cannot mint.
      lua_pushlightuserdata(state, fixture_.get());
      lua_pushcclosure(state, Reattach, "test.reattach_same_executable", 1);
      lua_setfield(state, -2, "test_reattach_same_executable");
      lua_pop(state, 3);
      return true;
    }

  private:
    static auto Reattach(lua_State* state) -> int
    {
      auto* fixture = static_cast<ReattachFixture*>(
        lua_touserdata(state, lua_upvalueindex(1)));
      if (!fixture->node.DetachScripting()
        || !fixture->node.AttachScripting()) {
        lua_pushboolean(state, 0);
        return 1;
      }
      auto scripting = fixture->node.GetScripting();
      const auto added = scripting.AddSlot(fixture->asset);
      const auto ready = added
        && scripting.MarkSlotReady(
          scripting.Slots().front(), fixture->executable);
      lua_pushboolean(state, ready ? 1 : 0);
      return 1;
    }

    std::shared_ptr<ReattachFixture> fixture_;
  };

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

NOLINT_TEST_F(
  ScriptingModuleTest, SlotFailuresPreserveHealthyScriptsCompilerAndLaterScenes)
{
  using data::pak::scripting::ScriptCompression;
  using data::pak::scripting::ScriptLanguage;
  constexpr auto kFrameCount = 3;
  constexpr uint64_t kBadScriptHash = 41;
  constexpr uint64_t kHealthyScriptHash = 42;

  enum class Fault { kLoad, kGameplay, kSceneMutation };
  for (const auto fault :
    { Fault::kLoad, Fault::kGameplay, Fault::kSceneMutation }) {
    SCOPED_TRACE(static_cast<int>(fault));
    auto module = MakeModule();
    ASSERT_TRUE(AttachModule(module));
    std::shared_ptr<const ScriptExecutable> failing;
    if (fault == Fault::kLoad) {
      auto bytes = ToBytes("bad cached bytecode");
      bytes.insert(bytes.begin(), 0);
      auto blob = std::make_shared<const ScriptBytecodeBlob>(
        ScriptBytecodeBlob::FromOwned(std::move(bytes), ScriptLanguage::kLuau,
          ScriptCompression::kNone, kBadScriptHash,
          ScriptBlobOrigin::kEmbeddedResource,
          ScriptBlobCanonicalName { "invalid-cache-fixture" }));
      failing = std::make_shared<const CompiledScriptExecutable>(blob);
    } else {
      const auto hook
        = fault == Fault::kSceneMutation ? "on_scene_mutation" : "on_gameplay";
      const auto source = std::string("local script = {}\nfunction script.")
        + hook + R"lua((_ctx, _dt)
  _G.__failing_ticks = (_G.__failing_ticks or 0) + 1
  error("broken scene content")
end
return script
)lua";
      failing = CompileSlotExecutable(source, kBadScriptHash);
    }
    ASSERT_NE(failing, nullptr);
    auto scene = MakeSceneWithExecutableSlot("fault-isolation", failing);
    ASSERT_NE(scene, nullptr);
    auto healthy_node = scene->CreateNode("healthy-script");
    ASSERT_TRUE(healthy_node.AttachScripting());
    auto healthy = healthy_node.GetScripting();
    ASSERT_TRUE(healthy.AddSlot(MakeScriptAsset()));
    auto healthy_executable = CompileSlotExecutable(R"lua(
return function(_ctx, _dt)
  _G.__healthy_ticks = (_G.__healthy_ticks or 0) + 1
end
)lua",
      kHealthyScriptHash);
    ASSERT_NE(healthy_executable, nullptr);
    ASSERT_TRUE(healthy.MarkSlotReady(
      healthy.Slots().front(), std::move(healthy_executable)));

    engine::FrameContext frame;
    frame.SetScene(observer_ptr<Scene> { scene.get() });
    TestEventLoop loop;
    for (int tick = 0; tick < kFrameCount; ++tick) {
      oxygen::co::Run(loop, [&]() -> co::Co<> {
        co_await module.OnGameplay(observer_ptr { &frame });
        co_await module.OnSceneMutation(observer_ptr { &frame });
      });
    }
    const auto errors = frame.GetErrors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front().kind, engine::FrameErrorKind::kContentFailure);
    EXPECT_EQ(errors.front().source_key, "ScriptingModule");
    EXPECT_TRUE(FakeEngine().GetScriptCompilationService().HasCompiler(
      ScriptLanguage::kLuau));
    auto result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { R"lua(
assert(_G.__healthy_ticks == 3, "healthy slot stopped running")
assert((_G.__failing_ticks or 0) <= 1, "faulted slot ran again")
)lua" },
      .chunk_name = ScriptChunkName { "verify_slot_fault_isolation" },
    });
    EXPECT_TRUE(result.ok) << result.message;

    auto next_scene = MakeSceneWithExecutableSlot("later-scene",
      CompileSlotExecutable(
        "return function(_ctx, _dt) _G.__later_scene = 42 end",
        kHealthyScriptHash));
    ASSERT_NE(next_scene, nullptr);
    engine::FrameContext next_frame;
    next_frame.SetScene(observer_ptr<Scene> { next_scene.get() });
    oxygen::co::Run(loop, [&]() -> co::Co<> {
      co_await module.OnGameplay(observer_ptr { &next_frame });
    });
    EXPECT_FALSE(next_frame.HasErrors());
    result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { "assert(_G.__later_scene == 42)" },
      .chunk_name = ScriptChunkName { "verify_later_scene" },
    });
    EXPECT_TRUE(result.ok) << result.message;
  }
}

NOLINT_TEST_F(ScriptingModuleTest,
  SlotInitializationAndHookFaultsReleaseOwnedEventListeners)
{
  constexpr uint64_t kFaultHash = 51;
  enum class Fault : uint8_t { kInitialization, kGameplay, kMutation };
  for (const auto fault :
    { Fault::kInitialization, Fault::kGameplay, Fault::kMutation }) {
    SCOPED_TRACE(static_cast<int>(fault));
    auto module = MakeModule();
    ASSERT_TRUE(AttachModule(module));
    auto source = std::string(R"lua(
local function leaked_listener() _G.__leaked = (_G.__leaked or 0) + 1 end
oxygen.events.on("frame.start", leaked_listener, { phase = "frame_start" })
)lua");
    if (fault == Fault::kInitialization) {
      source += "error('initialization fault')";
    } else {
      source += std::string("local script = {}\nfunction script.")
        + (fault == Fault::kGameplay ? "on_gameplay" : "on_scene_mutation")
        + R"lua((_ctx, _dt)
  oxygen.events.on("frame.start", leaked_listener, { phase = "frame_start" })
  error("slot hook fault")
end
return script
)lua";
    }
    auto executable = CompileSlotExecutable(source, kFaultHash);
    ASSERT_NE(executable, nullptr);
    auto scene = MakeSceneWithExecutableSlot("listener-hook-fault", executable);
    ASSERT_NE(scene, nullptr);
    engine::FrameContext frame;
    frame.SetScene(observer_ptr<Scene> { scene.get() });
    RunSlotHooks(module, frame);
    ASSERT_EQ(frame.GetErrors().size(), 1U);
    EXPECT_EQ(
      frame.GetErrors().front().kind, engine::FrameErrorKind::kContentFailure);

    // The failed slot's owner scope must be restored before global
    // registration.
    auto result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { R"lua(
assert(oxygen.events.listener_count("frame.start") == 0, "faulted listeners retained")
oxygen.events.on("frame.start", function()
  _G.__global_alive = (_G.__global_alive or 0) + 1
end, { phase = "frame_start" })
)lua" },
      .chunk_name = ScriptChunkName { "global_listener_after_slot_fault" },
    });
    ASSERT_TRUE(result.ok) << result.message;
    module.OnFrameStart(observer_ptr { &frame });
    RunSlotHooks(module, frame);
    module.OnFrameStart(observer_ptr { &frame });
    ASSERT_EQ(frame.GetErrors().size(), 1U);
    result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { R"lua(
assert((_G.__leaked or 0) == 0, "faulted owner listener executed")
assert(_G.__global_alive == 2, "global listener lost its independent lifetime")
)lua" },
      .chunk_name = ScriptChunkName { "verify_hook_listener_cleanup" },
    });
    EXPECT_TRUE(result.ok) << result.message;
  }
}

NOLINT_TEST_F(ScriptingModuleTest,
  OwnedCallbackFaultStopsOnlyOwnerAndReplacementAndSceneCleanupWork)
{
  constexpr uint64_t kFaultHash = 61;
  constexpr uint64_t kHealthyHash = 62;
  constexpr uint64_t kReplacementHash = 63;
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto faulty = CompileSlotExecutable(R"lua(
local script = {}
oxygen.events.on("frame.start", function()
  _G.__bad_events = (_G.__bad_events or 0) + 1
  oxygen.events.on("frame.end", function() _G.__nested_bad = true end, { phase = "frame_end" })
  oxygen.events.on("frame.start", function() _G.__nested_bad = true end, { phase = "frame_start", priority = 20 })
  error("owned callback fault")
end, { phase = "frame_start", priority = 10 })
oxygen.events.on("frame.start", function() _G.__bad_sibling = true end, { phase = "frame_start" })
function script.on_gameplay(_ctx, _dt) _G.__bad_gameplay = (_G.__bad_gameplay or 0) + 1 end
function script.on_scene_mutation(_ctx, _dt) _G.__bad_mutation = (_G.__bad_mutation or 0) + 1 end
return script
)lua",
    kFaultHash);
  ASSERT_NE(faulty, nullptr);
  auto scene = MakeSceneWithExecutableSlot("owned-listener-fault", faulty);
  ASSERT_NE(scene, nullptr);
  auto healthy_node = scene->CreateNode("healthy-owner");
  ASSERT_TRUE(healthy_node.AttachScripting());
  auto healthy = healthy_node.GetScripting();
  ASSERT_TRUE(healthy.AddSlot(MakeScriptAsset()));
  const auto healthy_executable = CompileSlotExecutable(R"lua(
local script = {}
oxygen.events.on("frame.start", function()
  _G.__healthy_events = (_G.__healthy_events or 0) + 1
end, { phase = "frame_start", priority = 5 })
function script.on_gameplay(_ctx, _dt) _G.__healthy_gameplay = (_G.__healthy_gameplay or 0) + 1 end
function script.on_scene_mutation(_ctx, _dt) _G.__healthy_mutation = (_G.__healthy_mutation or 0) + 1 end
return script
)lua",
    kHealthyHash);
  ASSERT_NE(healthy_executable, nullptr);
  ASSERT_TRUE(
    healthy.MarkSlotReady(healthy.Slots().front(), healthy_executable));
  engine::FrameContext frame;
  frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, frame);
  module.OnFrameStart(observer_ptr { &frame });
  RunSlotHooks(module, frame);
  module.OnFrameEnd(observer_ptr { &frame });
  module.OnFrameStart(observer_ptr { &frame });
  ASSERT_EQ(frame.GetErrors().size(), 1U);
  EXPECT_EQ(
    frame.GetErrors().front().kind, engine::FrameErrorKind::kContentFailure);
  auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__bad_events == 1 and _G.__bad_sibling == nil, "same-batch owner isolation failed")
assert(_G.__nested_bad == nil, "callback-created listener lost owner attribution")
assert(_G.__bad_gameplay == 1 and _G.__bad_mutation == 1, "faulted owner hooks kept running")
assert(_G.__healthy_events == 2, "healthy owner event stopped")
assert(_G.__healthy_gameplay == 2 and _G.__healthy_mutation == 2, "healthy owner hooks stopped")
assert(oxygen.events.listener_count("frame.start") == 1, "faulted listener references retained")
assert(oxygen.events.listener_count("frame.end") == 0, "nested faulted listener retained")
)lua" },
    .chunk_name = ScriptChunkName { "verify_callback_fault_isolation" },
  });
  ASSERT_TRUE(result.ok) << result.message;

  auto failed_node = scene->GetRootNodes().front();
  ASSERT_EQ(failed_node.GetName(), "script-node");
  auto replacement_slot = failed_node.GetScripting();
  const auto replacement = CompileSlotExecutable(R"lua(
oxygen.events.on("frame.start", function()
  _G.__replacement_event = (_G.__replacement_event or 0) + 1
end, { phase = "frame_start" })
return function(_ctx, _dt) _G.__replacement_hook = true end
)lua",
    kReplacementHash);
  ASSERT_NE(replacement, nullptr);
  ASSERT_TRUE(replacement_slot.MarkSlotReady(
    replacement_slot.Slots().front(), replacement));
  scene->SyncObservers();
  RunSlotHooks(module, frame);
  module.OnFrameStart(observer_ptr { &frame });
  result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__replacement_hook and _G.__replacement_event == 1, "replacement owner did not recover")
assert(oxygen.events.listener_count("frame.start") == 2, "replacement leaked old listeners")
)lua" },
    .chunk_name = ScriptChunkName { "verify_listener_owner_replacement" },
  });
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(FakeEngine().GetScriptCompilationService().HasCompiler(
    data::pak::scripting::ScriptLanguage::kLuau));

  // Scene change must release listeners before the next frame-start dispatch.
  auto next_scene
    = std::make_shared<Scene>("later-scene", kDefaultSceneCapacity);
  engine::FrameContext next_frame;
  next_frame.SetScene(observer_ptr<Scene> { next_scene.get() });
  module.OnFrameStart(observer_ptr { &next_frame });
  EXPECT_FALSE(next_frame.HasErrors());
  result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(oxygen.events.listener_count("frame.start") == 0, "old scene listener references retained")
assert(_G.__replacement_event == 1, "old scene listener fired during scene activation")
assert(_G.__healthy_events == 3, "old healthy scene listener survived teardown")
)lua" },
    .chunk_name = ScriptChunkName { "verify_scene_listener_cleanup" },
  });
  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(ScriptingModuleTest,
  SlotRebuildAndDetachReleaseListenersWithoutRemovingGlobalSubscriptions)
{
  constexpr uint64_t kOriginalHash = 71;
  constexpr uint64_t kReplacementHash = 72;
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto original = CompileSlotExecutable(R"lua(
oxygen.events.on("frame.start", function() _G.__old_listener = true end, { phase = "frame_start" })
return function(_ctx, _dt) end
)lua",
    kOriginalHash);
  ASSERT_NE(original, nullptr);
  ASSERT_EQ(original->ContentHash(), kOriginalHash);
  auto scene = MakeSceneWithExecutableSlot("listener-lifetime", original);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, frame);
  auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.events.on("frame.start", function()
  _G.__global_lifetime_hits = (_G.__global_lifetime_hits or 0) + 1
end, { phase = "frame_start" })
assert(oxygen.events.listener_count("frame.start") == 2)
)lua" },
    .chunk_name = ScriptChunkName { "register_global_lifetime_listener" },
  });
  ASSERT_TRUE(result.ok) << result.message;

  auto node = scene->GetRootNodes().front();
  auto scripting = node.GetScripting();
  const auto replacement = CompileSlotExecutable(R"lua(
oxygen.events.on("frame.start", function()
  _G.__new_listener_hits = (_G.__new_listener_hits or 0) + 1
end, { phase = "frame_start" })
return function(_ctx, _dt) end
)lua",
    kReplacementHash);
  ASSERT_NE(replacement, nullptr);
  ASSERT_EQ(replacement->ContentHash(), kReplacementHash);
  ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(), replacement));
  scene->SyncObservers();
  result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local count = oxygen.events.listener_count("frame.start")
assert(count == 1, "rebuild retained old listener: count=" .. tostring(count))
)lua" },
    .chunk_name = ScriptChunkName { "verify_rebuild_listener_release" },
  });
  ASSERT_TRUE(result.ok) << result.message;
  RunSlotHooks(module, frame);
  module.OnFrameStart(observer_ptr { &frame });
  ASSERT_TRUE(node.DetachScripting());
  scene->SyncObservers();
  module.OnFrameStart(observer_ptr { &frame });
  EXPECT_FALSE(frame.HasErrors());
  result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__old_listener == nil, "replaced executable listener fired")
assert(_G.__new_listener_hits == 1, "detached slot listener fired")
assert(_G.__global_lifetime_hits == 2, "global subscription removed with slot")
assert(oxygen.events.listener_count("frame.start") == 1, "detached listener refs retained")
)lua" },
    .chunk_name = ScriptChunkName { "verify_detached_listener_release" },
  });
  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(ScriptingModuleTest,
  SelfDetachOrDestroyRejectsListenerRegistrationBeforeObserverSync)
{
  constexpr uint64_t kSelfRemovalHash = 81;
  for (const auto destroy_node : { false, true }) {
    SCOPED_TRACE(destroy_node);
    auto module = MakeModule();
    ASSERT_TRUE(AttachModule(module));
    auto result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { R"lua(
oxygen.events.on("frame.start", function()
  _G.__global_after_self_remove = (_G.__global_after_self_remove or 0) + 1
end, { phase = "frame_start" })
)lua" },
      .chunk_name = ScriptChunkName { "global_listener_before_self_remove" },
    });
    ASSERT_TRUE(result.ok) << result.message;
    const auto* const removal = destroy_node
      ? "assert(oxygen.scene.destroy_node(node), 'self destroy failed')"
      : "assert(node:detach_scripting(), 'self detach failed')";
    const auto source = std::string(R"lua(
local script = {}
local registered = false
function script.on_scene_mutation(ctx, _dt)
  if registered then return end
  registered = true
  local node = oxygen.scene.current_node(ctx)
  oxygen.events.on("demo.self_remove", function()
)lua") + removal
      + R"lua(
    oxygen.events.on("demo.retired_registration", function() end, { phase = "frame_start" })
    _G.__retired_registration_succeeded = true
  end, { phase = "scene_mutation" })
  oxygen.events.emit("demo.self_remove", nil, { phase = "scene_mutation" })
end
return script
)lua";
    const auto executable = CompileSlotExecutable(source, kSelfRemovalHash);
    ASSERT_NE(executable, nullptr);
    auto scene = MakeSceneWithExecutableSlot("self-removing-owner", executable);
    ASSERT_NE(scene, nullptr);
    engine::FrameContext frame;
    frame.SetScene(observer_ptr<Scene> { scene.get() });
    RunSlotHooks(module, frame);
    const auto errors = frame.GetErrors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front().kind, engine::FrameErrorKind::kContentFailure);
    EXPECT_NE(errors.front().message.find("retired script instance"),
      std::string::npos);
    // No observer sync occurred inside the callback: the registration-time
    // liveness predicate must detect the removed node/component itself.
    scene->SyncObservers();
    module.OnFrameStart(observer_ptr { &frame });
    result = module.ExecuteScript(ScriptExecutionRequest {
      .source_text = ScriptSourceText { R"lua(
assert(_G.__retired_registration_succeeded == nil)
assert(oxygen.events.listener_count("demo.retired_registration") == 0)
assert(oxygen.events.listener_count("demo.self_remove") == 0)
assert(oxygen.events.listener_count("frame.start") == 1, "global owner was retired")
assert(_G.__global_after_self_remove == 1)
)lua" },
      .chunk_name
      = ScriptChunkName { "verify_self_removal_registration_rejected" },
    });
    EXPECT_TRUE(result.ok) << result.message;
  }
}

NOLINT_TEST_F(ScriptingModuleTest,
  SelfReattachWithSameExecutableCannotReviveRetiredInstance)
{
  constexpr uint64_t kExecutableHash = 91;
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto executable = CompileSlotExecutable(R"lua(
_G.__reattach_initializations = (_G.__reattach_initializations or 0) + 1
local script = {}
local registered = false
function script.on_scene_mutation(_ctx, _dt)
  _G.__reattach_mutation_ticks = (_G.__reattach_mutation_ticks or 0) + 1
  if _G.__reattached_same_executable then return end
  if registered then return end
  registered = true
  oxygen.events.on("demo.self_reattach", function()
    assert(oxygen.events.test_reattach_same_executable(), "native self-reattach failed")
    _G.__reattached_same_executable = true
    oxygen.events.on("demo.revived_owner", function() end)
    _G.__revived_owner = true
  end, { phase = "scene_mutation" })
  oxygen.events.emit("demo.self_reattach", nil, { phase = "scene_mutation" })
end
return script
)lua",
    kExecutableHash);
  ASSERT_NE(executable, nullptr);
  auto scene = MakeSceneWithExecutableSlot("self-reattach-owner", executable);
  ASSERT_NE(scene, nullptr);
  auto node = scene->GetRootNodes().front();
  const auto old_slot = node.GetScripting().Slots().front();
  const auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  if (!impl) {
    return;
  }
  const auto old_incarnation
    = impl->get().GetComponent<scene::ScriptingComponent>().IncarnationId();
  auto fixture = std::make_shared<ReattachFixture>(ReattachFixture {
    .node = node, .asset = old_slot.Asset(), .executable = executable });
  ASSERT_TRUE(
    module.RegisterBindingPack(std::make_shared<ReattachBindingPack>(fixture)));
  engine::FrameContext frame;
  frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, frame);
  const auto errors = frame.GetErrors();
  ASSERT_EQ(errors.size(), 1U);
  EXPECT_EQ(errors.front().kind, engine::FrameErrorKind::kContentFailure);
  EXPECT_NE(
    errors.front().message.find("retired script instance"), std::string::npos);
  const auto& replacement
    = impl->get().GetComponent<scene::ScriptingComponent>();
  EXPECT_NE(replacement.IncarnationId(), old_incarnation);
  // Both the reused component-local slot ID and executable would otherwise
  // match the old owner; the runtime incarnation must distinguish replacement.
  EXPECT_EQ(
    replacement.TryGetSlotIndex(old_slot), scene::ScriptSlotIndex { 0 });
  ASSERT_EQ(replacement.Slots().size(), 1U);
  EXPECT_EQ(replacement.Slots().front().Executable(), executable);
  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__reattached_same_executable == true)
assert(_G.__revived_owner == nil)
assert(oxygen.events.listener_count("demo.revived_owner") == 0)
assert(oxygen.events.listener_count("demo.self_reattach") == 0)
)lua" },
    .chunk_name = ScriptChunkName { "verify_component_incarnation_isolation" },
  });
  EXPECT_TRUE(result.ok) << result.message;

  scene->SyncObservers();
  engine::FrameContext replacement_frame;
  replacement_frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, replacement_frame);
  RunSlotHooks(module, replacement_frame);
  EXPECT_FALSE(replacement_frame.HasErrors());
  const auto recovered = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__reattach_initializations == 2, "new component did not initialize exactly once")
assert(_G.__reattach_mutation_ticks == 3, "new component hooks remained quarantined")
assert(oxygen.events.listener_count("demo.revived_owner") == 0)
)lua" },
    .chunk_name
    = ScriptChunkName { "verify_same_executable_incarnation_recovery" },
  });
  EXPECT_TRUE(recovered.ok) << recovered.message;
}

NOLINT_TEST_F(
  ScriptingModuleTest, SameExecutableNewSlotInSameComponentStartsFreshInstance)
{
  constexpr uint64_t kExecutableHash = 101;
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto executable = CompileSlotExecutable(R"lua(
_G.__slot_incarnation_initializations = (_G.__slot_incarnation_initializations or 0) + 1
oxygen.events.on("frame.start", function() end, { phase = "frame_start" })
return function(_ctx, _dt)
  _G.__slot_incarnation_ticks = (_G.__slot_incarnation_ticks or 0) + 1
end
)lua",
    kExecutableHash);
  ASSERT_NE(executable, nullptr);
  auto scene
    = MakeSceneWithExecutableSlot("same-executable-new-slot", executable);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, frame);
  auto node = scene->GetRootNodes().front();
  auto scripting = node.GetScripting();
  const auto old_slot = scripting.Slots().front();
  ASSERT_TRUE(scripting.RemoveSlot(old_slot));
  ASSERT_TRUE(scripting.AddSlot(old_slot.Asset()));
  ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(), executable));
  scene->SyncObservers();
  RunSlotHooks(module, frame);
  RunSlotHooks(module, frame);
  EXPECT_FALSE(frame.HasErrors());
  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__slot_incarnation_initializations == 2, "new slot reused old instance")
assert(_G.__slot_incarnation_ticks == 3, "new slot hooks did not continue")
assert(oxygen.events.listener_count("frame.start") == 1, "old slot listener retained")
)lua" },
    .chunk_name = ScriptChunkName { "verify_same_executable_new_slot" },
  });
  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(ScriptingModuleTest,
  InPlaceBytecodeReloadRebuildsHealthyAndFaultedSlotsWithoutObserverSync)
{
  constexpr uint64_t kOriginalHash = 111;
  constexpr uint64_t kReplacementHash = 112;
  enum class Fault : uint8_t { kNone, kInitialization, kGameplay, kMutation };
  for (const auto declared_hash : { uint64_t { 0 }, kOriginalHash }) {
    SCOPED_TRACE(declared_hash);
    for (const auto first_phase :
      { PhaseId::kGameplay, PhaseId::kSceneMutation }) {
      SCOPED_TRACE(static_cast<int>(first_phase));
      for (const auto fault : { Fault::kNone, Fault::kInitialization,
             Fault::kGameplay, Fault::kMutation }) {
        SCOPED_TRACE(static_cast<int>(fault));
        auto module = MakeModule();
        ASSERT_TRUE(AttachModule(module));
        auto source = std::string(R"lua(
_G.__original_initializations = (_G.__original_initializations or 0) + 1
oxygen.events.on("frame.start", function() _G.__old_reload_listener = true end,
  { phase = "frame_start" })
local script = {}
function script.on_gameplay(_ctx, _dt)
  _G.__original_gameplay = (_G.__original_gameplay or 0) + 1
end
function script.on_scene_mutation(_ctx, _dt)
  _G.__original_mutation = (_G.__original_mutation or 0) + 1
end
)lua");
        if (fault == Fault::kInitialization) {
          source += "error('original initialization fault')\n";
        } else if (fault != Fault::kNone) {
          source += std::string("function script.")
            + (fault == Fault::kGameplay ? "on_gameplay" : "on_scene_mutation")
            + R"lua((_ctx, _dt)
  _G.__original_faults = (_G.__original_faults or 0) + 1
  error("original hook fault")
end
)lua";
        }
        source += "return script\n";
        const auto original = CompileSlotBytecode(source, declared_hash);
        ASSERT_NE(original, nullptr);
        ASSERT_EQ(original->ContentHash(), declared_hash);
        auto executable = std::make_shared<CompiledScriptExecutable>(original);
        auto scene = MakeSceneWithExecutableSlot("in-place-reload", executable);
        ASSERT_NE(scene, nullptr);
        engine::FrameContext original_frame;
        original_frame.SetScene(observer_ptr<Scene> { scene.get() });
        RunSlotHooks(module, original_frame);
        RunSlotHooks(module, original_frame);
        const auto errors = original_frame.GetErrors();
        ASSERT_EQ(errors.size(), fault == Fault::kNone ? 0U : 1U);
        if (!errors.empty()) {
          EXPECT_EQ(
            errors.front().kind, engine::FrameErrorKind::kContentFailure);
        }
        auto result = module.ExecuteScript(ScriptExecutionRequest {
          .source_text = ScriptSourceText { R"lua(
assert(_G.__original_initializations == 1, "unchanged slot was reinitialized")
assert((_G.__original_faults or 0) <= 1, "unchanged faulted slot was retried")
)lua" },
          .chunk_name = ScriptChunkName { "verify_unchanged_reload_source" },
        });
        ASSERT_TRUE(result.ok) << result.message;

        const auto replacement = CompileSlotBytecode(R"lua(
_G.__reload_initializations = (_G.__reload_initializations or 0) + 1
oxygen.events.on("frame.start", function()
  _G.__reload_events = (_G.__reload_events or 0) + 1
end, { phase = "frame_start" })
local script = {}
function script.on_gameplay(_ctx, _dt)
  _G.__reload_gameplay = (_G.__reload_gameplay or 0) + 1
end
function script.on_scene_mutation(_ctx, _dt)
  _G.__reload_mutation = (_G.__reload_mutation or 0) + 1
end
return script
)lua",
          declared_hash == 0 ? 0 : kReplacementHash);
        ASSERT_NE(replacement, nullptr);
        ASSERT_EQ(replacement->ContentHash(),
          declared_hash == 0 ? 0 : kReplacementHash);
        // Matches the reload callback's in-place replacement. Neither this nor
        // the observer-free hook calls below changes the attached slot object.
        executable->UpdateBytecode(replacement);
        engine::FrameContext reloaded_frame;
        reloaded_frame.SetScene(observer_ptr<Scene> { scene.get() });
        module.OnFrameStart(observer_ptr { &reloaded_frame });
        result = module.ExecuteScript(ScriptExecutionRequest {
          .source_text = ScriptSourceText { R"lua(
assert(_G.__old_reload_listener == nil, "old bytecode listener survived reload")
assert(oxygen.events.listener_count("frame.start") == 0, "old listener refs retained")
)lua" },
          .chunk_name = ScriptChunkName { "verify_reload_before_next_hook" },
        });
        ASSERT_TRUE(result.ok) << result.message;
        TestEventLoop loop;
        if (first_phase == PhaseId::kGameplay) {
          co::Run(loop, module.OnGameplay(observer_ptr { &reloaded_frame }));
        } else {
          co::Run(
            loop, module.OnSceneMutation(observer_ptr { &reloaded_frame }));
        }
        result = module.ExecuteScript(ScriptExecutionRequest {
          .source_text = ScriptSourceText { R"lua(
assert(_G.__reload_initializations == 1, "first hook did not rebuild reloaded slot")
assert((_G.__reload_gameplay or 0) + (_G.__reload_mutation or 0) == 1, "first reloaded hook did not execute")
_G.__reload_gameplay = 0
_G.__reload_mutation = 0
)lua" },
          .chunk_name = ScriptChunkName { "verify_first_reloaded_hook" },
        });
        ASSERT_TRUE(result.ok) << result.message;
        RunSlotHooks(module, reloaded_frame);
        RunSlotHooks(module, reloaded_frame);
        module.OnFrameStart(observer_ptr { &reloaded_frame });
        EXPECT_FALSE(reloaded_frame.HasErrors());
        result = module.ExecuteScript(ScriptExecutionRequest {
          .source_text = ScriptSourceText { R"lua(
assert(_G.__original_initializations == 1, "old slot was reinitialized")
assert(_G.__reload_initializations == 1, "reloaded slot initialized more than once")
assert(_G.__reload_gameplay == 2 and _G.__reload_mutation == 2, "reloaded hooks stayed stopped")
assert(_G.__reload_events == 1, "reloaded listener did not execute")
assert(oxygen.events.listener_count("frame.start") == 1, "reload leaked listener references")
)lua" },
          .chunk_name = ScriptChunkName { "verify_in_place_reload_recovery" },
        });
        EXPECT_TRUE(result.ok) << result.message;
      }
    }
  }
}

NOLINT_TEST_F(ScriptingModuleTest,
  SameHashExecutableReplacementRebuildsWithoutObserverNotification)
{
  constexpr uint64_t kExecutableHash = 121;
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto bytecode = CompileSlotBytecode(R"lua(
_G.__same_hash_initializations = (_G.__same_hash_initializations or 0) + 1
local generation = _G.__same_hash_initializations
oxygen.events.on("frame.start", function() _G.__same_hash_event = generation end,
  { phase = "frame_start" })
return function(_ctx, _dt) _G.__same_hash_hook = generation end
)lua",
    kExecutableHash);
  ASSERT_NE(bytecode, nullptr);
  const auto original
    = std::make_shared<const CompiledScriptExecutable>(bytecode);
  const auto replacement
    = std::make_shared<const CompiledScriptExecutable>(bytecode);
  ASSERT_NE(original, replacement);
  ASSERT_EQ(original->ContentHash(), replacement->ContentHash());
  auto scene = MakeSceneWithExecutableSlot("same-hash-replacement", original);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr<Scene> { scene.get() });
  RunSlotHooks(module, frame);
  scene->SyncObservers();
  auto node = scene->GetRootNodes().front();
  auto scripting = node.GetScripting();
  ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(), replacement));
  // MarkSlotReady emits no Changed mutation for this same-hash replacement.
  RunSlotHooks(module, frame);
  RunSlotHooks(module, frame);
  module.OnFrameStart(observer_ptr { &frame });
  EXPECT_FALSE(frame.HasErrors());
  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
assert(_G.__same_hash_initializations == 2, "same-hash replacement did not initialize once")
assert(_G.__same_hash_hook == 2, "old executable hooks remained active")
assert(_G.__same_hash_event == 2, "replacement listener did not execute")
assert(oxygen.events.listener_count("frame.start") == 1, "replacement retained old listener")
)lua" },
    .chunk_name = ScriptChunkName { "verify_same_hash_executable_replacement" },
  });
  EXPECT_TRUE(result.ok) << result.message;
}

} // namespace oxygen::scripting::test
