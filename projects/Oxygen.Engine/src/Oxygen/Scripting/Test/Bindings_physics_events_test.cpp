//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace oxygen::scripting::test {

class PhysicsEventsBindingsTest : public ScriptingModuleTest { };

namespace {

  using oxygen::co::testing::TestEventLoop;

  auto RunGameplayPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(
      loop, [&]() -> co::Co<> { co_await module.OnGameplay(context); });
  }

  auto RunSceneMutationPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(
      loop, [&]() -> co::Co<> { co_await module.OnSceneMutation(context); });
  }

} // namespace

NOLINT_TEST_F(PhysicsEventsBindingsTest,
  ExecuteScriptEventsEmitRejectsReservedPhysicsPrefix)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.events.emit("physics.contact_begin")
)lua" },
    .chunk_name = ScriptChunkName { "events_reserved_physics_prefix" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
}

NOLINT_TEST_F(
  PhysicsEventsBindingsTest, PhysicsEventsDrainWithoutModuleReturnsEmptyTable)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local drained = oxygen.physics.events.drain()
if type(drained) ~= "table" then
  error("events.drain should return table when module is unavailable")
end
if #drained ~= 0 then
  error("events.drain should be empty when no physics module is present")
end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_events_drain_without_module_empty_table" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsEventsBindingsTest,
  PhysicsEventsDrainAllowedPhasesReturnTableWithoutErrors)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_gameplay()
  local drained = oxygen.physics.events.drain()
  if type(drained) ~= "table" then
    error("events.drain must return table in gameplay")
  end
end

function on_scene_mutation()
  local drained = oxygen.physics.events.drain()
  if type(drained) ~= "table" then
    error("events.drain must return table in scene_mutation")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_events_drain_allowed_phases" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  engine::FrameContext context;
  RunGameplayPhase(module, observer_ptr<engine::FrameContext> { &context });
  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

NOLINT_TEST_F(
  PhysicsEventsBindingsTest, PhysicsEventsDrainBlockedFrameStartReturnsEmpty)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_frame_start()
  local drained = oxygen.physics.events.drain()
  if type(drained) ~= "table" then
    error("events.drain must return table in blocked phases")
  end
  if #drained ~= 0 then
    error("events.drain must be empty in blocked phases")
  end
end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_events_drain_blocked_frame_start" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

} // namespace oxygen::scripting::test
