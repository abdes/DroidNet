//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsSoftBodyBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsSoftBodyBindingsTest, PhysicsSoftBodyBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local soft = oxygen.physics.soft_body
if type(soft.create) ~= "function" then error("missing soft_body.create") end
if type(soft.destroy) ~= "function" then error("missing soft_body.destroy") end
if type(soft.set_material_params) ~= "function" then error("missing soft_body.set_material_params") end
if type(soft.get_state) ~= "function" then error("missing soft_body.get_state") end
if type(soft.get_authority) ~= "function" then error("missing soft_body.get_authority") end
if type(soft.flush_structural_changes) ~= "function" then error("missing soft_body.flush_structural_changes") end

if soft.create({ cluster_count = 1 }) ~= nil then
  error("soft_body.create should return nil without module")
end
if soft.flush_structural_changes() ~= nil then
  error("soft_body.flush_structural_changes should return nil without module")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_soft_body_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsSoftBodyBindingsTest, PhysicsSoftBodyBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local soft = oxygen.physics.soft_body

local ok = pcall(function() soft.destroy(nil) end)
if ok then error("soft_body.destroy must reject invalid handle") end

ok = pcall(function() soft.set_material_params(nil, {}) end)
if ok then error("soft_body.set_material_params must reject invalid handle") end

ok = pcall(function() soft.get_state(nil) end)
if ok then error("soft_body.get_state must reject invalid handle") end

ok = pcall(function() soft.get_authority(nil) end)
if ok then error("soft_body.get_authority must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_soft_body_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
