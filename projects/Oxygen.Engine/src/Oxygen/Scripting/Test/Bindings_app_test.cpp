//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class AppBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(AppBindingsTest, ExecuteScriptAppBindingsExposeEngineState)
{
  auto module = MakeModule();

  // NOLINTBEGIN(*-magic-numbers)
  FakeEngine().MutableConfig().application.name = "My Test App";
  FakeEngine().MutableConfig().application.version = 42;
  FakeEngine().MutableConfig().target_fps = 60;
  FakeEngine().MutableConfig().frame_count = 1000;
  FakeEngine().MutableConfig().enable_asset_loader = true;
  // NOLINTEND(*-magic-numbers)

  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local app = oxygen.app

if app.name() ~= "My Test App" then
  error("app.name mismatch: " .. tostring(app.name()))
end
if app.version() ~= 42 then
  error("app.version mismatch")
end
if app.target_fps() ~= 60 then
  error("app.target_fps mismatch")
end
if app.frame_count_limit() ~= 1000 then
  error("app.frame_count_limit mismatch")
end
if app.asset_loader_enabled() ~= true then
  error("app.asset_loader_enabled mismatch")
end
if app.max_target_fps() ~= 240 then
  error("app.max_target_fps mismatch")
end
)lua" },
    .chunk_name = ScriptChunkName { "app_surface" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(AppBindingsTest, ExecuteScriptAppRequestStopFlipsRunningState)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if oxygen.app.is_running() ~= true then
  error("expected running before request_stop")
end
oxygen.app.request_stop()
if oxygen.app.is_running() ~= false then
  error("expected stopped after request_stop")
end
)lua" },
    .chunk_name = ScriptChunkName { "app_request_stop" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
