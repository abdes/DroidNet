//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class LogBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(LogBindingsTest, ExecuteScriptLogBindingWorksWithFakeEngine)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.log.trace("trace message", 1, true)
oxygen.log.debug("debug message", nil, "ok")
oxygen.log.info("info message", 3.14)
oxygen.log.warn("warn message", {})
oxygen.log.error("error message", function() end)
oxygen.log.info() -- Empty log test
)lua" },
    .chunk_name = ScriptChunkName { "log_all_levels" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  LogBindingsTest, LogBindingHandlesHeterogeneousArgumentsWithoutAbortingScript)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local tbl = { x = 1, y = 2 }
local fn = function() end
oxygen.log.info("before", tbl, fn, nil, true, 12.5, "after")
local marker = 7
if marker ~= 7 then
  error("script should continue after log call")
end
)lua" },
    .chunk_name = ScriptChunkName { "log_heterogeneous_args_resilience" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(LogBindingsTest, LogBindingUsesSandboxCallableTostringContract)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
tostring = 42
oxygen.log.info("sandbox keeps logging safe")
)lua" },
    .chunk_name = ScriptChunkName { "log_sandbox_tostring_contract" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(LogBindingsTest, LogBindingsReturnNoValuesAndArePcallSafe)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local levels = {
  oxygen.log.trace,
  oxygen.log.debug,
  oxygen.log.info,
  oxygen.log.warn,
  oxygen.log.error
}

for i = 1, #levels do
  local fn = levels[i]
  local ok, ret = pcall(fn, "msg", i, true, nil, { k = i })
  if not ok then
    error("pcall failed for log level index " .. tostring(i))
  end
  if ret ~= nil then
    error("log function must not return values")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "log_pcall_and_return_contract" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(LogBindingsTest, LogBindingsAreStableWhenCalledFromFrameStartHook)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_frame_start()
  oxygen.log.trace("frame_start trace")
  oxygen.log.debug("frame_start debug")
  oxygen.log.info("frame_start info")
  oxygen.log.warn("frame_start warn")
  oxygen.log.error("frame_start error")
end
)lua" },
    .chunk_name = ScriptChunkName { "log_frame_start_hook" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

NOLINT_TEST_F(LogBindingsTest, LogBindingsRepeatedCallsDoNotAbortScript)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
for i = 1, 200 do
  oxygen.log.info("burst", i, i % 2 == 0)
end

local marker = 123
if marker ~= 123 then
  error("script state corrupted after repeated log calls")
end
)lua" },
    .chunk_name = ScriptChunkName { "log_repeated_call_stability" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
