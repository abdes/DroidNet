//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class InputBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(InputBindingsTest, InputBindingsRegisterActionListeners)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

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
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  InputBindingsTest, InputBindingsOnceActionWithOptionsRegistersConnection)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
input_once_conn = oxygen.input.once_action(
  "JumpAction",
  oxygen.input.edges.triggered,
  function(_) end,
  { phase = "frame_start", priority = 11 }
)

if input_once_conn == nil then
  error("once_action should return connection")
end
if not input_once_conn:connected() then
  error("once_action connection should be connected after registration")
end

input_once_conn:disconnect()
if input_once_conn:connected() then
  error("once_action connection should disconnect cleanly")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_once_register_connection" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(InputBindingsTest, InputBindingsEventNameRejectsInvalidShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ok = pcall(function()
  oxygen.input.event_name(123, oxygen.input.edges.triggered)
end)
if ok then
  error("event_name should reject non-string action")
end

ok = pcall(function()
  oxygen.input.event_name("JumpAction", 123)
end)
if ok then
  error("event_name should reject non-string edge")
end

ok = pcall(function()
  oxygen.input.event_name("", oxygen.input.edges.triggered)
end)
if ok then
  error("event_name should reject empty action")
end

ok = pcall(function()
  oxygen.input.event_name("JumpAction", "bad_edge")
end)
if ok then
  error("event_name should reject unsupported edge")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_event_name_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(InputBindingsTest, InputBindingsOnAndOnceRejectInvalidShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local fn = function() end
local ok = pcall(function()
  oxygen.input.on_action(77, oxygen.input.edges.triggered, fn)
end)
if ok then
  error("on_action should reject non-string action")
end

ok = pcall(function()
  oxygen.input.on_action("JumpAction", 77, fn)
end)
if ok then
  error("on_action should reject non-string edge")
end

ok = pcall(function()
  oxygen.input.on_action("JumpAction", oxygen.input.edges.triggered, "bad")
end)
if ok then
  error("on_action should reject non-function callback")
end

ok = pcall(function()
  oxygen.input.on_action("JumpAction", "bad_edge", fn)
end)
if ok then
  error("on_action should reject unsupported edge")
end

ok = pcall(function()
  oxygen.input.on_action(
    "JumpAction",
    oxygen.input.edges.triggered,
    fn,
    "bad_options"
  )
end)
if ok then
  error("on_action should reject non-table options")
end

ok = pcall(function()
  oxygen.input.once_action(
    "JumpAction",
    oxygen.input.edges.triggered,
    fn,
    "bad_options"
  )
end)
if ok then
  error("once_action should reject non-table options")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_on_once_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
