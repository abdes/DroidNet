//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsBodyBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local body = oxygen.physics.body
if type(body.attach) ~= "function" then error("missing body.attach") end
if type(body.get) ~= "function" then error("missing body.get") end

if body.get(nil) ~= nil then
  error("body.get should return nil for non-node input")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_body_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyBindingsRejectInvalidNodeAndHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local body = oxygen.physics.body

if body.attach(nil, {}) ~= nil then
  error("body.attach must return nil for invalid scene node shape")
end

if body.get(nil) ~= nil then
  error("body.get must return nil for invalid scene node shape")
end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_body_invalid_node_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
