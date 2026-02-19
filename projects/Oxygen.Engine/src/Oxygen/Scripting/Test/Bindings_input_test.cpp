//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

NOLINT_TEST_F(ScriptingModuleTest, InputBindingsRegisterActionListeners)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local event_name = oxygen.input.event_name("RotateSpeedUpAction", oxygen.input.edges.triggered)
if event_name ~= "input.action.RotateSpeedUpAction.triggered" then
  error("unexpected input event name")
end

local conn = oxygen.input.on_action("RotateSpeedUpAction", oxygen.input.edges.triggered, function() end)
if conn == nil then
  error("expected connection object")
end
if not conn:connected() then
  error("expected connected listener")
end
conn:disconnect()
if conn:connected() then
  error("expected disconnected listener")
end

local ok = pcall(function()
  oxygen.input.on_action("RotateSpeedUpAction", "invalid_edge", function() end)
end)
if ok then
  error("expected invalid edge registration failure")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_bindings_test" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

} // namespace oxygen::scripting::test
