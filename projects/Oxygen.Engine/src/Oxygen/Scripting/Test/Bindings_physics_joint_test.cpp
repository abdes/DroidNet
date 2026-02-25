//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsJointBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsJointBindingsTest, PhysicsJointBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local joint = oxygen.physics.joint
if type(joint.create) ~= "function" then error("missing joint.create") end
if type(joint.destroy) ~= "function" then error("missing joint.destroy") end
if type(joint.set_enabled) ~= "function" then error("missing joint.set_enabled") end

if joint.create({ type = "fixed", body_a_id = 1, body_b_id = 2 }) ~= nil then
  error("joint.create should return nil without module")
end

local ok = pcall(function()
  joint.destroy(nil)
end)
if ok then
  error("joint.destroy should reject invalid handle shape")
end

ok = pcall(function()
  joint.set_enabled(nil, true)
end)
if ok then
  error("joint.set_enabled should reject invalid handle shape")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_joint_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
