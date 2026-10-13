//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsAggregateBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(PhysicsAggregateBindingsTest,
  PhysicsAggregateBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local aggregate = oxygen.physics.aggregate
if type(aggregate.create) ~= "function" then error("missing aggregate.create") end
if type(aggregate.destroy) ~= "function" then error("missing aggregate.destroy") end
if type(aggregate.add_member_body) ~= "function" then error("missing aggregate.add_member_body") end
if type(aggregate.remove_member_body) ~= "function" then error("missing aggregate.remove_member_body") end
if type(aggregate.get_member_bodies) ~= "function" then error("missing aggregate.get_member_bodies") end
if type(aggregate.flush_structural_changes) ~= "function" then error("missing aggregate.flush_structural_changes") end

if aggregate.create() ~= nil then error("aggregate.create should return nil without module") end

local ok = pcall(function()
  aggregate.destroy(nil)
end)
if ok then
  error("aggregate.destroy should reject invalid handle shape")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_aggregate_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsAggregateBindingsTest,
  PhysicsAggregateBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local aggregate = oxygen.physics.aggregate

local ok = pcall(function() aggregate.destroy(nil) end)
if ok then error("aggregate.destroy must reject invalid handle") end

ok = pcall(function() aggregate.add_member_body(nil, nil) end)
if ok then error("aggregate.add_member_body must reject invalid handle") end

ok = pcall(function() aggregate.remove_member_body(nil, nil) end)
if ok then error("aggregate.remove_member_body must reject invalid handle") end

ok = pcall(function() aggregate.get_member_bodies(nil) end)
if ok then error("aggregate.get_member_bodies must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_aggregate_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
