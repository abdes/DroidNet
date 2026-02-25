//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class PhysicsCharacterBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  PhysicsCharacterBindingsTest, PhysicsCharacterBindingsRejectInvalidNodeShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ch = oxygen.physics.character
if type(ch.attach) ~= "function" then error("missing character.attach") end
if type(ch.get) ~= "function" then error("missing character.get") end

if ch.attach(nil, {}) ~= nil then
  error("character.attach must return nil for invalid node shape")
end

if ch.get(nil) ~= nil then
  error("character.get must return nil for invalid node shape")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_character_invalid_node_shapes" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsCharacterBindingsTest,
  PhysicsCharacterBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ch = oxygen.physics.character
if type(ch.attach) ~= "function" then error("missing character.attach") end
if type(ch.get) ~= "function" then error("missing character.get") end

if ch.get(nil) ~= nil then
  error("character.get should return nil for non-node input")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_character_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
