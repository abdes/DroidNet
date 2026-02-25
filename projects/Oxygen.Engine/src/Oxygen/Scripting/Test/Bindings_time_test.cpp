//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class TimeBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(TimeBindingsTest, ExecuteScriptTimeBindingRequiresFrameContext)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local _ = oxygen.time.delta_seconds()
)lua" },
    .chunk_name = ScriptChunkName { "time_no_context" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
  EXPECT_NE(
    result.message.find("requires active FrameContext"), std::string::npos);
}

NOLINT_TEST_F(
  TimeBindingsTest, TimeBindingsAllAccessorsRejectMissingFrameContextViaPCall)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local calls = {
  function() oxygen.time.delta_seconds() end,
  function() oxygen.time.fixed_delta_seconds() end,
  function() oxygen.time.frame_sequence_number() end,
  function() oxygen.time.time_scale() end,
  function() oxygen.time.is_paused() end,
  function() oxygen.time.interpolation_alpha() end,
  function() oxygen.time.current_fps() end,
}

for i = 1, #calls do
  local ok, err = pcall(calls[i])
  if ok then
    error("time accessor should fail without frame context at index " .. tostring(i))
  end
  if type(err) ~= "string" then
    error("expected error string for missing frame context at index " .. tostring(i))
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "time_all_no_context_pcall" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(TimeBindingsTest, OnFrameStartTimeBindingUsesFrameContextValues)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  engine::FrameContext context;
  context.SetFrameSequenceNumber(frame::SequenceNumber { 42 }, Tag::Get());
  context.SetModuleTimingData(
    ModuleTimingData {
      .game_delta_time = time::CanonicalDuration { 10ms },
      .fixed_delta_time = time::CanonicalDuration { 20ms },
      .time_scale = 1.5f,
      .is_paused = true,
      .interpolation_alpha = 0.25f,
      .current_fps = 120.0f,
    },
    Tag::Get());

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_frame_start()
  local dt = oxygen.time.delta_seconds()
  local fdt = oxygen.time.fixed_delta_seconds()
  local seq = oxygen.time.frame_sequence_number()
  local scale = oxygen.time.time_scale()
  local paused = oxygen.time.is_paused()
  local alpha = oxygen.time.interpolation_alpha()
  local fps = oxygen.time.current_fps()

  if math.abs(dt - 0.01) > 0.0001 then
    error("bad dt: " .. tostring(dt))
  end
  if math.abs(fdt - 0.02) > 0.0001 then
    error("bad fixed dt: " .. tostring(fdt))
  end
  if seq ~= 42 then
    error("bad sequence: " .. tostring(seq))
  end
  if math.abs(scale - 1.5) > 0.0001 then
    error("bad scale: " .. tostring(scale))
  end
  if not paused then
    error("bad paused state")
  end
  if math.abs(alpha - 0.25) > 0.0001 then
    error("bad alpha: " .. tostring(alpha))
  end
  if math.abs(fps - 120.0) > 0.0001 then
    error("bad fps: " .. tostring(fps))
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "time_context_values" },
  });
  ASSERT_TRUE(hook_result.ok);

  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

} // namespace oxygen::scripting::test
