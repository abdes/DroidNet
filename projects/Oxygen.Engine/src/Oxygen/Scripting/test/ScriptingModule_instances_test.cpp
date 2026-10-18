//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScriptingModule_slot_helpers.h"
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/test/ScriptingModule_test_fixture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::test {

namespace {

  //! Records `oxygen.events.test_probe(text)` calls in C++, so hooks that run
  //! after the Lua state closes are still observable.
  class ProbeBindingPack final
    : public bindings::contracts::IScriptBindingPack {
  public:
    explicit ProbeBindingPack(std::shared_ptr<std::vector<std::string>> log)
      : log_(std::move(log))
    {
    }

    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "test.instance-probe";
    }

    [[nodiscard]] auto Register(
      const bindings::contracts::ScriptBindingPackContext& context) const
      -> bool override
    {
      auto* state = context.lua_state;
      lua_getref(state, context.runtime_env_ref);
      lua_getfield(state, -1, "oxygen");
      lua_getfield(state, -1, "events");
      lua_pushlightuserdata(state, log_.get());
      lua_pushcclosure(state, Probe, "test.probe", 1);
      lua_setfield(state, -2, "test_probe");
      lua_pop(state, 3);
      return true;
    }

  private:
    static auto Probe(lua_State* state) -> int
    {
      auto* log = static_cast<std::vector<std::string>*>(
        lua_touserdata(state, lua_upvalueindex(1)));
      log->emplace_back(luaL_checkstring(state, 1));
      return 0;
    }

    std::shared_ptr<std::vector<std::string>> log_;
  };

  auto FloatDeclaration(const std::string_view key, const float value)
    -> data::ScriptParameters::value_type
  {
    return { std::string { key }, value };
  }

  // Distinct content hashes keep each test's executables apart.
  constexpr uint64_t kShutdownHash = 5;
  constexpr uint64_t kFailedStartHash = 6;
  constexpr uint64_t kPlainTableHash = 7;
  constexpr uint64_t kSessionEndHash = 8;
  constexpr uint64_t kSessionDiagnosticHash = 9;

  //! A script that probes its lifecycle and listener ownership.
  constexpr std::string_view kLifecycleScript = R"lua(
local script = oxygen.script {}
function script.on_start(self)
  oxygen.events.test_probe("start")
  oxygen.events.on("frame.start", function() end, { phase = "frame_start" })
end
function script.on_gameplay(self, _dt)
  oxygen.events.test_probe("gameplay")
end
function script.on_destroy(self)
  oxygen.events.test_probe("destroy")
end
return script
)lua";

  class ScriptInstanceTest : public ScriptingModuleTest {
  protected:
    auto AttachWithProbe(ScriptingModule& module) -> void
    {
      ASSERT_TRUE(AttachModule(module));
      ASSERT_TRUE(
        module.RegisterBindingPack(std::make_shared<ProbeBindingPack>(log_)));
    }

    [[nodiscard]] auto Log() const -> const std::vector<std::string>&
    {
      return *log_;
    }

    auto ClearLog() -> void { log_->clear(); }

    static auto ExpectLua(ScriptingModule& module, const std::string& source)
      -> void
    {
      const auto result = module.ExecuteScript(ScriptExecutionRequest {
        .source_text = ScriptSourceText { source },
        .chunk_name = ScriptChunkName { "verify" },
      });
      EXPECT_TRUE(result.ok) << result.message;
    }

  private:
    std::shared_ptr<std::vector<std::string>> log_
      = std::make_shared<std::vector<std::string>>();
  };

  //! Adds a ready slot with the given order and `tag` override to `node`.
  auto AddTaggedSlot(scene::SceneNode& node, const int32_t execution_order,
    const std::string& tag,
    const std::shared_ptr<const ScriptExecutable>& executable) -> bool
  {
    if (!node.HasScripting() && !node.AttachScripting()) {
      return false;
    }
    auto scripting = node.GetScripting();
    if (!scripting.AddSlot(MakeScriptAsset(), execution_order)) {
      return false;
    }
    const auto& slot = scripting.Slots().back();
    return scripting.SetParameter(slot, "tag", data::ScriptParam { tag })
      && scripting.MarkSlotReady(scripting.Slots().back(), executable);
  }

} // namespace

//! Instances start before any on_gameplay of the phase, and every hook runs
//! by execution order, then node order, then slot position.
NOLINT_TEST_F(ScriptInstanceTest, HooksRunInExecutionNodeAndSlotOrder)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto executable = CompileSlotExecutable(R"lua(
local script = oxygen.script {
  params = { tag = oxygen.param.string {} },
}
local function log(what, self)
  table.insert(_G.__log, what .. ":" .. self.params.tag)
end
function script.on_start(self) log("start", self) end
function script.on_fixed_simulation(self, _dt) log("fixed", self) end
function script.on_gameplay(self, _dt) log("gameplay", self) end
function script.on_scene_mutation(self, _dt) log("mutation", self) end
return script
)lua",
    1);
  ASSERT_NE(executable, nullptr);
  auto scene = std::make_shared<scene::Scene>("order", kDefaultSceneCapacity);
  auto first = scene->CreateNode("first");
  auto second = scene->CreateNode("second");
  ASSERT_TRUE(AddTaggedSlot(first, 5, "late", executable));
  ASSERT_TRUE(AddTaggedSlot(second, 0, "second", executable));
  ASSERT_TRUE(AddTaggedSlot(first, 0, "first", executable));
  ExpectLua(module, "_G.__log = {}");

  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  co::testing::TestEventLoop loop;
  for (int i = 0; i < 2; ++i) {
    co::Run(loop, module.OnFixedSimulation(observer_ptr { &frame }));
    RunSlotHooks(module, frame);
  }

  EXPECT_FALSE(frame.HasErrors());
  ExpectLua(module, R"lua(
local expected = table.concat({
  "start:first", "start:second", "start:late",
  "gameplay:first", "gameplay:second", "gameplay:late",
  "mutation:first", "mutation:second", "mutation:late",
  "fixed:first", "fixed:second", "fixed:late",
  "gameplay:first", "gameplay:second", "gameplay:late",
  "mutation:first", "mutation:second", "mutation:late",
}, ",")
local actual = table.concat(_G.__log, ",")
assert(actual == expected, actual)
)lua");
}

//! `self` carries the owning node and read-only effective parameters, and
//! methods resolve on the script table.
NOLINT_TEST_F(ScriptInstanceTest, SelfCarriesNodeAndReadOnlyParameters)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto executable = CompileSlotExecutable(R"lua(
local script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1.5 },
    target = oxygen.param.asset { kind = "material" },
  },
}
function script:describe()
  return self.node:get_name()
end
function script.on_start(self)
  assert(self:describe() == "script-node", "self.node is not the owner")
  assert(self.params.speed == 1.5, "declared default missing")
  assert(self.params.target == "01234567-89ab-cdef-0123-456789abcdef",
    "asset override is not its key")
  assert(not pcall(function() self.params.speed = 2 end),
    "self.params is writable")
  self.started = true
end
function script.on_gameplay(self, _dt)
  assert(self.started, "instance state was not kept")
  _G.__self_checked = true
end
return script
)lua",
    2);
  ASSERT_NE(executable, nullptr);
  const auto declarations
    = data::ScriptParameters { FloatDeclaration("speed", 1.5F) };
  auto scene = std::make_shared<scene::Scene>("self", kDefaultSceneCapacity);
  auto node = scene->CreateNode("script-node");
  ASSERT_TRUE(node.AttachScripting());
  auto scripting = node.GetScripting();
  ASSERT_TRUE(scripting.AddSlot(MakeScriptAsset(declarations)));
  const auto target
    = data::AssetKey::FromString("01234567-89ab-cdef-0123-456789abcdef");
  ASSERT_TRUE(target.has_value());
  ASSERT_TRUE(scripting.SetParameter(scripting.Slots().front(), "target",
    data::ScriptParam { data::ScriptAssetParam {
      .key = target.value(),
      .asset_type = data::AssetType::kMaterial,
    } }));
  ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(), executable));

  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);

  EXPECT_FALSE(frame.HasErrors());
  ExpectLua(module, "assert(_G.__self_checked == true)");
}

//! A node parameter is the scene node it is bound to, and nil when unbound.
NOLINT_TEST_F(ScriptInstanceTest, NodeParameterIsItsBoundNode)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto executable = CompileSlotExecutable(R"lua(
local script = oxygen.script {
  params = {
    target = oxygen.param.node {},
    unbound = oxygen.param.node {},
  },
}
function script.on_start(self)
  assert(self.params.target:get_name() == "target-node",
    "the node parameter is not its bound node")
  assert(self.params.unbound == nil, "an unbound node parameter is not nil")
  _G.__node_checked = true
end
return script
)lua",
    3);
  ASSERT_NE(executable, nullptr);
  auto scene = std::make_shared<scene::Scene>("nodes", kDefaultSceneCapacity);
  auto node = scene->CreateNode("script-node");
  const auto target = scene->CreateNode("target-node");
  ASSERT_TRUE(node.AttachScripting());
  auto scripting = node.GetScripting();
  ASSERT_TRUE(scripting.AddSlot(MakeScriptAsset()));
  const auto& slot = scripting.Slots().front();
  for (const auto* name : { "target", "unbound" }) {
    ASSERT_TRUE(scripting.SetParameter(slot, name,
      data::ScriptParam { data::ScriptNodeParam { .node_index = 1U } }));
  }
  ASSERT_TRUE(scripting.BindNodeParameter(slot, "target", target));
  ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(), executable));

  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);

  EXPECT_FALSE(frame.HasErrors());
  ExpectLua(module, "assert(_G.__node_checked == true)");
}

//! A started instance gets on_destroy when it ends by any route, and its
//! listeners are released afterwards.
NOLINT_TEST_F(ScriptInstanceTest, StartedInstanceEndsWithOnDestroy)
{
  enum class End : uint8_t {
    kSlotRemoved,
    kNodeDestroyed,
    kSceneChanged,
    kExecutableChanged,
  };
  for (const auto end : {
         End::kSlotRemoved,
         End::kNodeDestroyed,
         End::kSceneChanged,
         End::kExecutableChanged,
       }) {
    TRACE_GCHECK_F(
      [&] {
        auto module = MakeModule();
        AttachWithProbe(module);
        auto scene = MakeSceneWithExecutableSlot(
          "destroy", CompileSlotExecutable(kLifecycleScript, 3));
        ASSERT_NE(scene, nullptr);
        engine::FrameContext frame;
        frame.SetScene(observer_ptr { scene.get() });
        RunSlotHooks(module, frame);
        // Deliver the slot's activation, as a frame's observer sync would.
        scene->SyncObservers();

        auto node = scene->GetRootNodes().front();
        auto scripting = node.GetScripting();
        auto other_scene
          = std::make_shared<scene::Scene>("other", kDefaultSceneCapacity);
        switch (end) {
        case End::kSlotRemoved:
          ASSERT_TRUE(scripting.RemoveSlot(scripting.Slots().front()));
          scene->SyncObservers();
          break;
        case End::kNodeDestroyed:
          ASSERT_TRUE(scene->DestroyNode(node));
          scene->SyncObservers();
          break;
        case End::kSceneChanged:
          frame.SetScene(observer_ptr { other_scene.get() });
          RunSlotHooks(module, frame);
          break;
        case End::kExecutableChanged:
          ASSERT_TRUE(scripting.MarkSlotReady(scripting.Slots().front(),
            CompileSlotExecutable(
              std::string(kLifecycleScript) + "-- changed\n", 4)));
          scene->SyncObservers();
          break;
        }

        EXPECT_FALSE(frame.HasErrors());
        EXPECT_THAT(
          Log(), ::testing::ElementsAre("start", "gameplay", "destroy"));
        ExpectLua(
          module, "assert(oxygen.events.listener_count('frame.start') == 0)");
        module.OnShutdown();
        ClearLog();
      }(),
      static_cast<int>(end));
  }
}

//! Module shutdown ends started instances.
NOLINT_TEST_F(ScriptInstanceTest, ShutdownEndsStartedInstances)
{
  auto module = MakeModule();
  AttachWithProbe(module);
  auto scene = MakeSceneWithExecutableSlot(
    "shutdown", CompileSlotExecutable(kLifecycleScript, kShutdownHash));
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);

  module.OnShutdown();

  EXPECT_THAT(Log(), ::testing::ElementsAre("start", "gameplay", "destroy"));
}

//! An instance whose on_start fails is faulted: it gets no further hooks and
//! no on_destroy.
NOLINT_TEST_F(ScriptInstanceTest, FailedStartGetsNoFurtherHooks)
{
  auto module = MakeModule();
  AttachWithProbe(module);
  auto scene = MakeSceneWithExecutableSlot("failed-start",
    CompileSlotExecutable(R"lua(
local script = oxygen.script {}
function script.on_start(self) error("start failed") end
function script.on_gameplay(self, _dt) oxygen.events.test_probe("gameplay") end
function script.on_destroy(self) oxygen.events.test_probe("destroy") end
return script
)lua",
      kFailedStartHash));
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);
  scene->SyncObservers();
  RunSlotHooks(module, frame);
  auto node = scene->GetRootNodes().front();
  auto scripting = node.GetScripting();
  ASSERT_TRUE(scripting.RemoveSlot(scripting.Slots().front()));
  scene->SyncObservers();

  ASSERT_EQ(frame.GetErrors().size(), 1U);
  EXPECT_NE(
    frame.GetErrors().front().message.find("on_start"), std::string::npos);
  EXPECT_TRUE(Log().empty());
}

//! A chunk must return the table made by `oxygen.script`.
NOLINT_TEST_F(ScriptInstanceTest, ChunkMustReturnScriptTable)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  auto scene = MakeSceneWithExecutableSlot("plain-table",
    CompileSlotExecutable(
      "return { on_gameplay = function() _G.__ran = true end }",
      kPlainTableHash));
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);

  ASSERT_EQ(frame.GetErrors().size(), 1U);
  EXPECT_NE(
    frame.GetErrors().front().message.find("oxygen.script"), std::string::npos);
  ExpectLua(module, "assert(_G.__ran == nil)");
}

//! Ending a play session ends every started instance while its node is
//! still reachable.
NOLINT_TEST_F(ScriptInstanceTest, SessionEndEndsInstancesWithNodeReachable)
{
  auto module = MakeModule();
  AttachWithProbe(module);
  auto scene = MakeSceneWithExecutableSlot("session-end",
    CompileSlotExecutable(R"lua(
local script = oxygen.script {}
function script.on_start(self) oxygen.events.test_probe("start") end
function script.on_destroy(self)
  oxygen.events.test_probe(self.node:is_alive() and "destroy" or "dead")
end
return script
)lua",
      kSessionEndHash));
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });
  RunSlotHooks(module, frame);

  module.OnSessionEnd(observer_ptr<IAsyncEngine> { &FakeEngine() });

  EXPECT_FALSE(frame.HasErrors());
  EXPECT_THAT(Log(), ::testing::ElementsAre("start", "destroy"));
}

//! A script global set in one session is gone in the next.
NOLINT_TEST_F(ScriptInstanceTest, SessionEndClearsScriptGlobals)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  ExpectLua(module, "_G.__session_value = 42");

  module.OnSessionEnd(observer_ptr<IAsyncEngine> { &FakeEngine() });

  ExpectLua(module, "assert(_G.__session_value == nil)");
}

//! Diagnostic handlers and binding packs survive the end of a session.
NOLINT_TEST_F(ScriptInstanceTest, DiagnosticHandlersSurviveSessionEnd)
{
  auto module = MakeModule();
  AttachWithProbe(module);
  int diagnostics = 0;
  const auto handler = module.AddDiagnosticHandler(
    [&diagnostics](
      const ScriptDiagnostic& /*diagnostic*/) -> void { ++diagnostics; });
  module.OnSessionEnd(observer_ptr<IAsyncEngine> { &FakeEngine() });
  auto scene = MakeSceneWithExecutableSlot("session-diagnostic",
    CompileSlotExecutable(R"lua(
local script = oxygen.script {}
function script.on_start(self)
  oxygen.events.test_probe("start")
  error("second session failure")
end
return script
)lua",
      kSessionDiagnosticHash));
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);

  EXPECT_EQ(diagnostics, 1);
  EXPECT_THAT(Log(), ::testing::ElementsAre("start"));
  EXPECT_TRUE(module.RemoveDiagnosticHandler(handler));
}

} // namespace oxygen::scripting::test
