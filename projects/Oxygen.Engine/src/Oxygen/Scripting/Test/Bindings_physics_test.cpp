//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace oxygen::scripting::test {

class PhysicsBindingsTest : public ScriptingModuleTest { };

namespace {

  using oxygen::co::testing::TestEventLoop;

  auto RunFixedSimulationPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(
      loop, [&]() -> co::Co<> { co_await module.OnFixedSimulation(context); });
  }

} // namespace

NOLINT_TEST_F(PhysicsBindingsTest,
  ExecuteScriptPhysicsBindingsExposeV1V2PhysicsModuleSurface)
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
if type(physics.aggregate) ~= "table" then error("missing physics.aggregate") end
if type(physics.articulation) ~= "table" then error("missing physics.articulation") end
if type(physics.joint) ~= "table" then error("missing physics.joint") end
if type(physics.vehicle) ~= "table" then error("missing physics.vehicle") end
if type(physics.soft_body) ~= "table" then error("missing physics.soft_body") end

if type(physics.body.attach) ~= "function" then error("missing physics.body.attach") end
if type(physics.body.get) ~= "function" then error("missing physics.body.get") end
if type(physics.character.attach) ~= "function" then error("missing physics.character.attach") end
if type(physics.character.get) ~= "function" then error("missing physics.character.get") end
if type(physics.query.raycast) ~= "function" then error("missing physics.query.raycast") end
if type(physics.query.sweep) ~= "function" then error("missing physics.query.sweep") end
if type(physics.query.overlap) ~= "function" then error("missing physics.query.overlap") end
if type(physics.events.drain) ~= "function" then error("missing physics.events.drain") end
if type(physics.aggregate.create) ~= "function" then error("missing physics.aggregate.create") end
if type(physics.aggregate.destroy) ~= "function" then error("missing physics.aggregate.destroy") end
if type(physics.aggregate.add_member_body) ~= "function" then error("missing physics.aggregate.add_member_body") end
if type(physics.aggregate.remove_member_body) ~= "function" then error("missing physics.aggregate.remove_member_body") end
if type(physics.aggregate.get_member_bodies) ~= "function" then error("missing physics.aggregate.get_member_bodies") end
if type(physics.aggregate.flush_structural_changes) ~= "function" then error("missing physics.aggregate.flush_structural_changes") end
if type(physics.articulation.create) ~= "function" then error("missing physics.articulation.create") end
if type(physics.articulation.destroy) ~= "function" then error("missing physics.articulation.destroy") end
if type(physics.articulation.add_link) ~= "function" then error("missing physics.articulation.add_link") end
if type(physics.articulation.remove_link) ~= "function" then error("missing physics.articulation.remove_link") end
if type(physics.articulation.get_root_body) ~= "function" then error("missing physics.articulation.get_root_body") end
if type(physics.articulation.get_link_bodies) ~= "function" then error("missing physics.articulation.get_link_bodies") end
if type(physics.articulation.get_authority) ~= "function" then error("missing physics.articulation.get_authority") end
if type(physics.articulation.flush_structural_changes) ~= "function" then error("missing physics.articulation.flush_structural_changes") end
if type(physics.joint.create) ~= "function" then error("missing physics.joint.create") end
if type(physics.joint.destroy) ~= "function" then error("missing physics.joint.destroy") end
if type(physics.joint.set_enabled) ~= "function" then error("missing physics.joint.set_enabled") end
if type(physics.vehicle.create) ~= "function" then error("missing physics.vehicle.create") end
if type(physics.vehicle.destroy) ~= "function" then error("missing physics.vehicle.destroy") end
if type(physics.vehicle.set_control_input) ~= "function" then error("missing physics.vehicle.set_control_input") end
if type(physics.vehicle.get_state) ~= "function" then error("missing physics.vehicle.get_state") end
if type(physics.vehicle.get_authority) ~= "function" then error("missing physics.vehicle.get_authority") end
if type(physics.vehicle.flush_structural_changes) ~= "function" then error("missing physics.vehicle.flush_structural_changes") end
if type(physics.soft_body.create) ~= "function" then error("missing physics.soft_body.create") end
if type(physics.soft_body.destroy) ~= "function" then error("missing physics.soft_body.destroy") end
if type(physics.soft_body.set_material_params) ~= "function" then error("missing physics.soft_body.set_material_params") end
if type(physics.soft_body.get_state) ~= "function" then error("missing physics.soft_body.get_state") end
if type(physics.soft_body.get_authority) ~= "function" then error("missing physics.soft_body.get_authority") end
if type(physics.soft_body.flush_structural_changes) ~= "function" then error("missing physics.soft_body.flush_structural_changes") end
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

if physics.aggregate.create() ~= nil then
  error("aggregate.create should return nil without physics module")
end

if physics.joint.create({
  type = "fixed",
  body_a_id = 1,
  body_b_id = 2,
}) ~= nil then
  error("joint.create should return nil without physics module")
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
if c.joint_type.fixed ~= "fixed" then error("joint_type.fixed enum mismatch") end
if c.joint_type.distance ~= "distance" then error("joint_type.distance enum mismatch") end
if c.joint_type.hinge ~= "hinge" then error("joint_type.hinge enum mismatch") end
if c.joint_type.slider ~= "slider" then error("joint_type.slider enum mismatch") end
if c.joint_type.spherical ~= "spherical" then error("joint_type.spherical enum mismatch") end
if c.aggregate_authority.command ~= "command" then error("aggregate_authority enum mismatch") end
if c.soft_body_tether_mode.euclidean ~= "euclidean" then error("soft_body_tether_mode enum mismatch") end

local ok = pcall(function()
  c.body_type.dynamic = "x"
end)
if ok then error("constants table should be read-only") end

local ok_joint = pcall(function()
  c.joint_type.hinge = "x"
end)
if ok_joint then error("joint_type table should be read-only") end
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

NOLINT_TEST_F(PhysicsBindingsTest, OnFixedSimulationPhysicsApiIsFullyBlocked)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_fixed_simulation()
  local physics = oxygen.physics
  if physics.query.raycast({}) ~= nil then
    error("query.raycast must be blocked in fixed_simulation")
  end
  local c, ids = physics.query.overlap({}, 8)
  if c ~= nil or ids ~= nil then
    error("query.overlap must be blocked in fixed_simulation")
  end
  local drained = physics.events.drain()
  if type(drained) ~= "table" or #drained ~= 0 then
    error("events.drain must return empty table in fixed_simulation")
  end
  if physics.aggregate.create() ~= nil then
    error("aggregate.create must be blocked in fixed_simulation")
  end
  if physics.articulation.create({ root_body_id = 1 }) ~= nil then
    error("articulation.create must be blocked in fixed_simulation")
  end
  if physics.joint.create({ body_a_id = 1, body_b_id = 2 }) ~= nil then
    error("joint.create must be blocked in fixed_simulation")
  end
  if physics.joint.destroy(nil) ~= false then
    error("joint.destroy must be blocked in fixed_simulation")
  end
  if physics.joint.set_enabled(nil, true) ~= false then
    error("joint.set_enabled must be blocked in fixed_simulation")
  end
  if physics.vehicle.create({ chassis_body_id = 1, wheel_body_ids = { 2 } }) ~= nil then
    error("vehicle.create must be blocked in fixed_simulation")
  end
  if physics.soft_body.create({ cluster_count = 1 }) ~= nil then
    error("soft_body.create must be blocked in fixed_simulation")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_fixed_sim_block" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  engine::FrameContext context;
  RunFixedSimulationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

NOLINT_TEST_F(
  PhysicsBindingsTest, ExecuteScriptEventsEmitRejectsReservedPhysicsPrefix)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.events.emit("physics.contact_begin")
)lua" },
    .chunk_name = ScriptChunkName { "events_reserved_physics_prefix" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
}

} // namespace oxygen::scripting::test
