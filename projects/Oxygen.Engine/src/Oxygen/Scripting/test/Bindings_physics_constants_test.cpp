//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsConstantsBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsConstantsBindingsTest, ExecuteScriptPhysicsConstantsAreReadOnly)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local c = oxygen.physics.constants
if c.body_type.static ~= "static" then error("body_type.static enum mismatch") end
if c.body_type.dynamic ~= "dynamic" then error("body_type enum mismatch") end
if c.body_type.kinematic ~= "kinematic" then error("body_type.kinematic enum mismatch") end
if c.body_flags.none ~= "none" then error("body_flags.none enum mismatch") end
if c.body_flags.enable_gravity ~= "enable_gravity" then error("body_flags.enable_gravity enum mismatch") end
if c.body_flags.is_trigger ~= "is_trigger" then error("body_flags.is_trigger enum mismatch") end
if c.body_flags.enable_ccd ~= "enable_ccd" then error("body_flags enum mismatch") end
if c.event_type.contact_begin ~= "contact_begin" then error("event_type enum mismatch") end
if c.event_type.contact_end ~= "contact_end" then error("event_type.contact_end enum mismatch") end
if c.event_type.trigger_begin ~= "trigger_begin" then error("event_type.trigger_begin enum mismatch") end
if c.event_type.trigger_end ~= "trigger_end" then error("event_type.trigger_end enum mismatch") end
if c.joint_type.fixed ~= "fixed" then error("joint_type.fixed enum mismatch") end
if c.joint_type.distance ~= "distance" then error("joint_type.distance enum mismatch") end
if c.joint_type.hinge ~= "hinge" then error("joint_type.hinge enum mismatch") end
if c.joint_type.slider ~= "slider" then error("joint_type.slider enum mismatch") end
if c.joint_type.spherical ~= "spherical" then error("joint_type.spherical enum mismatch") end
if c.aggregate_authority.simulation ~= "simulation" then error("aggregate_authority.simulation enum mismatch") end
if c.aggregate_authority.command ~= "command" then error("aggregate_authority enum mismatch") end
if c.soft_body_tether_mode.none ~= "none" then error("soft_body_tether_mode.none enum mismatch") end
if c.soft_body_tether_mode.euclidean ~= "euclidean" then error("soft_body_tether_mode enum mismatch") end
if c.soft_body_tether_mode.geodesic ~= "geodesic" then error("soft_body_tether_mode.geodesic enum mismatch") end
if c.shape_type.sphere ~= "sphere" then error("shape_type.sphere enum mismatch") end
if c.shape_type.capsule ~= "capsule" then error("shape_type.capsule enum mismatch") end
if c.shape_type.box ~= "box" then error("shape_type.box enum mismatch") end
if c.shape_type.cylinder ~= "cylinder" then error("shape_type.cylinder enum mismatch") end
if c.shape_type.cone ~= "cone" then error("shape_type.cone enum mismatch") end
if c.shape_type.convex_hull ~= "convex_hull" then error("shape_type.convex_hull enum mismatch") end
if c.shape_type.triangle_mesh ~= "triangle_mesh" then error("shape_type.triangle_mesh enum mismatch") end
if c.shape_type.height_field ~= "height_field" then error("shape_type.height_field enum mismatch") end
if c.shape_type.plane ~= "plane" then error("shape_type.plane enum mismatch") end
if c.shape_type.world_boundary ~= "world_boundary" then error("shape_type.world_boundary enum mismatch") end
if c.shape_type.compound ~= "compound" then error("shape_type enum mismatch") end
if c.shape_payload_type.convex ~= "convex" then error("shape_payload_type.convex enum mismatch") end
if c.shape_payload_type.mesh ~= "mesh" then error("shape_payload_type enum mismatch") end
if c.shape_payload_type.height_field ~= "height_field" then error("shape_payload_type.height_field enum mismatch") end
if c.shape_payload_type.compound ~= "compound" then error("shape_payload_type.compound enum mismatch") end
if c.world_boundary_mode.aabb_clamp ~= "aabb_clamp" then error("world_boundary_mode.aabb_clamp enum mismatch") end
if c.world_boundary_mode.plane_set ~= "plane_set" then error("world_boundary_mode enum mismatch") end

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
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
