//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsVehicleBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsVehicleBindingsTest, PhysicsVehicleBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local vehicle = oxygen.physics.vehicle
if type(vehicle.create) ~= "function" then error("missing vehicle.create") end
if type(vehicle.destroy) ~= "function" then error("missing vehicle.destroy") end
if type(vehicle.set_control_input) ~= "function" then error("missing vehicle.set_control_input") end
if type(vehicle.get_state) ~= "function" then error("missing vehicle.get_state") end
if type(vehicle.get_authority) ~= "function" then error("missing vehicle.get_authority") end
if type(vehicle.flush_structural_changes) ~= "function" then error("missing vehicle.flush_structural_changes") end

if vehicle.create({ chassis_body_id = 1, wheel_body_ids = { 2 } }) ~= nil then
  error("vehicle.create should return nil without module")
end
if vehicle.flush_structural_changes() ~= nil then
  error("vehicle.flush_structural_changes should return nil without module")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_vehicle_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsVehicleBindingsTest, PhysicsVehicleBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local vehicle = oxygen.physics.vehicle

local ok = pcall(function() vehicle.destroy(nil) end)
if ok then error("vehicle.destroy must reject invalid handle") end

ok = pcall(function() vehicle.set_control_input(nil, {}) end)
if ok then error("vehicle.set_control_input must reject invalid handle") end

ok = pcall(function() vehicle.get_state(nil) end)
if ok then error("vehicle.get_state must reject invalid handle") end

ok = pcall(function() vehicle.get_authority(nil) end)
if ok then error("vehicle.get_authority must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_vehicle_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
