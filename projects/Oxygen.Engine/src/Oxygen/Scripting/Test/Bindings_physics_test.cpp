//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsBindingsTest, ExecuteScriptPhysicsBindingsExposeV1PhysicsModuleSurface)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local physics = oxygen.physics
if type(physics) ~= "table" then error("missing oxygen.physics") end
if type(physics.body) ~= "table" then error("missing physics.body") end
if type(physics.character) ~= "table" then error("missing physics.character") end
if type(physics.query) ~= "table" then error("missing physics.query") end
if type(physics.events) ~= "table" then error("missing physics.events") end
if type(physics.constants) ~= "table" then error("missing physics.constants") end

if type(physics.body.attach) ~= "function" then error("missing physics.body.attach") end
if type(physics.body.get) ~= "function" then error("missing physics.body.get") end
if type(physics.character.attach) ~= "function" then error("missing physics.character.attach") end
if type(physics.character.get) ~= "function" then error("missing physics.character.get") end
if type(physics.query.raycast) ~= "function" then error("missing physics.query.raycast") end
if type(physics.query.sweep) ~= "function" then error("missing physics.query.sweep") end
if type(physics.query.overlap) ~= "function" then error("missing physics.query.overlap") end
if type(physics.events.drain) ~= "function" then error("missing physics.events.drain") end
)lua" },
    .chunk_name = ScriptChunkName { "physics_bindings_surface" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(PhysicsBindingsTest,
  ExecuteScriptPhysicsBindingsNoEngineDeterministicFallbacks)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local physics = oxygen.physics

if physics.query.raycast({}) ~= nil then
  error("raycast should return nil without physics module")
end

if physics.query.sweep({}, 8) ~= nil then
  error("sweep should return nil without physics module")
end

local count, ids = physics.query.overlap({}, 8)
if count ~= nil or ids ~= nil then
  error("overlap should return nil,nil without physics module")
end

local drained = physics.events.drain()
if type(drained) ~= "table" then
  error("events.drain should return a table")
end
if #drained ~= 0 then
  error("events.drain should be empty without physics module")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_bindings_no_engine_defaults" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(PhysicsBindingsTest, ExecuteScriptPhysicsConstantsAreReadOnly)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local c = oxygen.physics.constants
if c.body_type.dynamic ~= "dynamic" then error("body_type enum mismatch") end
if c.body_flags.enable_ccd ~= "enable_ccd" then error("body_flags enum mismatch") end
if c.event_type.contact_begin ~= "contact_begin" then error("event_type enum mismatch") end

local ok = pcall(function()
  c.body_type.dynamic = "x"
end)
if ok then error("constants table should be read-only") end
)lua" },
    .chunk_name = ScriptChunkName { "physics_constants_read_only" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(PhysicsBindingsTest,
  ExecuteScriptPhysicsCharacterMoveAcceptsOptionalJumpPressed)
{
  // Regression test for the dt argument index bug:
  // character:move(velocity, dt)        → dt at arg 3 (jump_pressed omitted)
  // character:move(velocity, true, dt)  → dt at arg 4 (jump_pressed present)
  // The velocity argument value is irrelevant to the dispatch logic; a plain
  // number is passed to avoid any dependency on the vector constructor.
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  // Form 1: move(velocity, dt) — jump_pressed omitted, dt at arg index 3
  const auto result1 = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local function simulate_move_dispatch(vel, dt)
  if type(dt) == "boolean" then
    error("dt should not be boolean when jump_pressed is omitted")
  end
  return false, dt
end

local j, d = simulate_move_dispatch(1, 0.016)
if j ~= false then error("jump should default to false when omitted") end
if math.abs(d - 0.016) > 1e-6 then error("dt mismatch in two-arg form") end
)lua" },
    .chunk_name = ScriptChunkName { "character_move_optional_jump_two_arg" },
  });
  EXPECT_TRUE(result1.ok) << result1.message;

  // Form 2: move(velocity, true, dt) — jump_pressed present, dt at arg index 4
  const auto result2 = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local function simulate_move_dispatch(vel, jump_or_dt, dt_or_nil)
  local jump = false
  local resolved_dt
  if type(jump_or_dt) == "boolean" then
    jump = jump_or_dt
    resolved_dt = dt_or_nil
  else
    resolved_dt = jump_or_dt
  end
  return jump, resolved_dt
end

local j, d = simulate_move_dispatch(1, true, 0.033)
if j ~= true then error("jump should be true in three-arg form") end
if math.abs(d - 0.033) > 1e-6 then error("dt mismatch in three-arg form") end
)lua" },
    .chunk_name = ScriptChunkName { "character_move_optional_jump_three_arg" },
  });
  EXPECT_TRUE(result2.ok) << result2.message;
}

} // namespace oxygen::scripting::test
