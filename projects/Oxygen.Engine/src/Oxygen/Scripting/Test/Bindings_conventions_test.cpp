//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class ConventionsBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  ConventionsBindingsTest, ExecuteScriptConventionsExposeEngineCoordinateLaw)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local c = oxygen.conventions
local eps = 0.0001

if c.handedness ~= "right_handed" then
  error("handedness mismatch")
end

if type(c.world.up) ~= "vector" or type(c.world.forward) ~= "vector"
   or type(c.world.right) ~= "vector" then
  error("world basis must be Luau vectors")
end

if type(c.view.up) ~= "vector" or type(c.view.forward) ~= "vector"
   or type(c.view.right) ~= "vector" then
  error("view basis must be Luau vectors")
end

if math.abs(c.world.up.x - 0.0) > eps
   or math.abs(c.world.up.y - 0.0) > eps
   or math.abs(c.world.up.z - 1.0) > eps then
  error("world.up mismatch")
end

if math.abs(c.world.forward.x - 0.0) > eps
   or math.abs(c.world.forward.y + 1.0) > eps
   or math.abs(c.world.forward.z - 0.0) > eps then
  error("world.forward mismatch")
end

if math.abs(c.world.right.x - 1.0) > eps
   or math.abs(c.world.right.y - 0.0) > eps
   or math.abs(c.world.right.z - 0.0) > eps then
  error("world.right mismatch")
end

if math.abs(c.view.up.x - 0.0) > eps
   or math.abs(c.view.up.y - 1.0) > eps
   or math.abs(c.view.up.z - 0.0) > eps then
  error("view.up mismatch")
end

if math.abs(c.view.forward.x - 0.0) > eps
   or math.abs(c.view.forward.y - 0.0) > eps
   or math.abs(c.view.forward.z + 1.0) > eps then
  error("view.forward mismatch")
end

if math.abs(c.view.right.x - 1.0) > eps
   or math.abs(c.view.right.y - 0.0) > eps
   or math.abs(c.view.right.z - 0.0) > eps then
  error("view.right mismatch")
end

if c.clip.z_near ~= 0.0 or c.clip.z_far ~= 1.0 then
  error("clip z range mismatch")
end
if c.clip.front_face_ccw ~= true then
  error("clip front face mismatch")
end
)lua" },
    .chunk_name = ScriptChunkName { "conventions_surface" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
