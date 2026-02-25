//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsQueryBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsQueryBindingsTest, PhysicsQueryBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local query = oxygen.physics.query
if type(query.raycast) ~= "function" then error("missing query.raycast") end
if type(query.sweep) ~= "function" then error("missing query.sweep") end
if type(query.overlap) ~= "function" then error("missing query.overlap") end

if query.raycast({}) ~= nil then error("raycast should return nil without module") end
if query.sweep({}, 8) ~= nil then error("sweep should return nil without module") end
local count, ids = query.overlap({}, 8)
if count ~= nil or ids ~= nil then error("overlap should return nil,nil without module") end
)lua" },
    .chunk_name = ScriptChunkName { "physics_query_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
