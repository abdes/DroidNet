//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsArticulationBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(PhysicsArticulationBindingsTest,
  PhysicsArticulationBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local articulation = oxygen.physics.articulation
if type(articulation.create) ~= "function" then error("missing articulation.create") end
if type(articulation.destroy) ~= "function" then error("missing articulation.destroy") end
if type(articulation.add_link) ~= "function" then error("missing articulation.add_link") end
if type(articulation.remove_link) ~= "function" then error("missing articulation.remove_link") end
if type(articulation.get_root_body) ~= "function" then error("missing articulation.get_root_body") end
if type(articulation.get_link_bodies) ~= "function" then error("missing articulation.get_link_bodies") end
if type(articulation.get_authority) ~= "function" then error("missing articulation.get_authority") end
if type(articulation.flush_structural_changes) ~= "function" then error("missing articulation.flush_structural_changes") end

if articulation.create({ root_body_id = 1 }) ~= nil then
  error("articulation.create should return nil without module")
end
if articulation.flush_structural_changes() ~= nil then
  error("articulation.flush_structural_changes should return nil without module")
end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_articulation_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsArticulationBindingsTest,
  PhysicsArticulationBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local articulation = oxygen.physics.articulation

local ok = pcall(function() articulation.destroy(nil) end)
if ok then error("articulation.destroy must reject invalid handle") end

ok = pcall(function() articulation.add_link(nil, {}) end)
if ok then error("articulation.add_link must reject invalid handle") end

ok = pcall(function() articulation.remove_link(nil, nil) end)
if ok then error("articulation.remove_link must reject invalid handle") end

ok = pcall(function() articulation.get_root_body(nil) end)
if ok then error("articulation.get_root_body must reject invalid handle") end

ok = pcall(function() articulation.get_link_bodies(nil) end)
if ok then error("articulation.get_link_bodies must reject invalid handle") end

ok = pcall(function() articulation.get_authority(nil) end)
if ok then error("articulation.get_authority must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_articulation_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
